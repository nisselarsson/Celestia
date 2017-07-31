/***************************************************************************
                          kdeglwidget.cpp  -  description
                             -------------------
    begin                : Tue Jul 16 2002
    copyright            : (C) 2002 by Christophe Teyssier
    email                : chris@teyssier.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <cassert>
#include <celengine/celestia.h>
#include <celengine/starbrowser.h>

#include <QCursor>
#include <QPaintDevice>
#include <QMouseEvent>
#include <QSettings>

#ifndef DEBUG
#  define G_DISABLE_ASSERT
#endif

#include "celmath/vecmath.h"
#include "celmath/quaternion.h"
#include "celmath/mathlib.h"
#include "celengine/astro.h"
#include "celutil/util.h"
#include "celutil/filetype.h"
#include "celutil/debug.h"
#include "celestia/imagecapture.h"
#include "celestia/celestiacore.h"
#include "celengine/simulation.h"
#include "celengine/glcontext.h"

#include "qtglwidget.h"

#include <math.h>
#include <vector>

using namespace Qt;


const int DEFAULT_RENDER_FLAGS =
		  Renderer::ShowStars              |
		  Renderer::ShowPlanets            |
		  Renderer::ShowGalaxies           |
		  Renderer::ShowGlobulars          |
		  Renderer::ShowCloudMaps          |
		  Renderer::ShowAtmospheres        |
		  Renderer::ShowEclipseShadows     |
		  Renderer::ShowRingShadows        |
		  Renderer::ShowCometTails         |
		  Renderer::ShowNebulae            |
		  Renderer::ShowOpenClusters       |
		  Renderer::ShowAutoMag            |
		  Renderer::ShowNightMaps          |
		  Renderer::ShowCloudShadows       |
		  Renderer::ShowTintedIllumination |
		  Renderer::ShowSmoothLines;        

const int DEFAULT_ORBIT_MASK = Body::Planet | Body::Moon | Body::Stellar;

const int DEFAULT_LABEL_MODE = 2176;

const float DEFAULT_AMBIENT_LIGHT_LEVEL = 0.1f;

const int DEFAULT_STARS_COLOR = ColorTable_Blackbody_D65;

const float DEFAULT_VISUAL_MAGNITUDE = 8.0f;

const Renderer::StarStyle DEFAULT_STAR_STYLE = Renderer::FuzzyPointStars;

const unsigned int DEFAULT_TEXTURE_RESOLUTION = medres;


CelestiaGlWidget::CelestiaGlWidget(QWidget* parent, const char* /* name */, CelestiaCore* core) :
    QGLWidget(parent)
{
    setFocusPolicy(Qt::ClickFocus);

    appCore = core;
    appRenderer = appCore->getRenderer();
    appSim = appCore->getSimulation();

    setCursor(QCursor(Qt::CrossCursor));
    currentCursor = CelestiaCore::CrossCursor;
    setMouseTracking(true);

    lastX = lastY = 0;
    cursorVisible = true;
}


/*!
  Release allocated resources
*/

CelestiaGlWidget::~CelestiaGlWidget()
{
}


/*!
  Paint the box. The actual openGL commands for drawing the box are
  performed here.
*/

void CelestiaGlWidget::paintGL()
{
    appCore->draw();
}


static GLContext::GLRenderPath getBestAvailableRenderPath(const GLContext& glc)
{
    const GLContext::GLRenderPath renderPaths[] = {
        GLContext::GLPath_GLSL,
        GLContext::GLPath_NvCombiner_ARBVP,
        GLContext::GLPath_DOT3_ARBVP,
        GLContext::GLPath_Multitexture,
        GLContext::GLPath_Basic,
    };

    for (unsigned i = 0; i < sizeof(renderPaths) / sizeof(renderPaths[0]); i++)
    {
        if (glc.renderPathSupported(renderPaths[i]))
            return renderPaths[i];
    }

    // No supported render path? Something has gone very wrong...
    assert(glc.renderPathSupported(GLContext::GLPath_Basic));

    return GLContext::GLPath_Basic;
}


/*!
  Set up the OpenGL rendering state, and define display list
*/

void CelestiaGlWidget::initializeGL()
{
    if (!appCore->initRenderer())
    {
        // cerr << "Failed to initialize renderer.\n";
        exit(1);
    }

    appCore->tick();

    // Read saved settings
    QSettings settings;
    appRenderer->setRenderFlags(settings.value("RenderFlags", DEFAULT_RENDER_FLAGS).toInt());
    appRenderer->setOrbitMask(settings.value("OrbitMask", DEFAULT_ORBIT_MASK).toInt());
    appRenderer->setLabelMode(settings.value("LabelMode", DEFAULT_LABEL_MODE).toInt());
    appRenderer->setAmbientLightLevel((float) settings.value("AmbientLightLevel", DEFAULT_AMBIENT_LIGHT_LEVEL).toDouble());
    appRenderer->setStarStyle((Renderer::StarStyle) settings.value("StarStyle", DEFAULT_STAR_STYLE).toInt());
    appRenderer->setResolution(settings.value("TextureResolution", DEFAULT_TEXTURE_RESOLUTION).toUInt());

    if (settings.value("StarsColor", DEFAULT_STARS_COLOR).toInt() == 0)
    appRenderer->setStarColorTable(GetStarColorTable(ColorTable_Enhanced));
    else
    appRenderer->setStarColorTable(GetStarColorTable(ColorTable_Blackbody_D65));

    appCore->getSimulation()->setFaintestVisible((float) settings.value("Preferences/VisualMagnitude", DEFAULT_VISUAL_MAGNITUDE).toDouble());

    // Read the saved render path
    GLContext::GLRenderPath bestPath = getBestAvailableRenderPath(*appRenderer->getGLContext());
    GLContext::GLRenderPath savedPath = (GLContext::GLRenderPath) settings.value("RenderPath", bestPath).toInt();

    // Use the saved path only if it's supported (otherwise a graphics card
    // downgrade could cause Celestia to not function.)
    GLContext::GLRenderPath usePath;
    if (appRenderer->getGLContext()->renderPathSupported(savedPath))
        usePath = savedPath;
    else
        usePath = bestPath;

    appRenderer->getGLContext()->setRenderPath(usePath);

    appCore->setScreenDpi(logicalDpiY());
}


void CelestiaGlWidget::resizeGL(int w, int h)
{
    appCore->resize(w, h);
}


void CelestiaGlWidget::mouseMoveEvent(QMouseEvent* m)
{
    int x = (int) m->x();
    int y = (int) m->y();

    int buttons = 0;
    if (m->buttons() & LeftButton)
        buttons |= CelestiaCore::LeftButton;
    if (m->buttons() & MidButton)
        buttons |= CelestiaCore::MiddleButton;
    if (m->buttons() & RightButton)
        buttons |= CelestiaCore::RightButton;
    if (m->modifiers() & ShiftModifier)
        buttons |= CelestiaCore::ShiftKey;
    if (m->modifiers() & ControlModifier)
        buttons |= CelestiaCore::ControlKey;
    
#ifdef TARGET_OS_MAC
    // On the Mac, right dragging is be simulated with Option+left drag.
    // We may want to enable this on other platforms, though it's mostly only helpful
    // for users with single button mice.
    if (m->modifiers() & AltModifier)
    {
        buttons &= ~CelestiaCore::LeftButton;
        buttons |= CelestiaCore::RightButton;
    }
#endif

    if ((m->buttons() & (LeftButton | RightButton)) != 0)
    {
        appCore->mouseMove(x - lastX, y - lastY, buttons);

        // Infinite mouse: allow the user to rotate and zoom continuously, i.e.,
        // without being stopped when the pointer reaches the window borders.
        QPoint pt;
        pt.setX(lastX);
        pt.setY(lastY);
        pt = mapToGlobal(pt);

        // First mouse drag event.
        // Hide the cursor and set its position to the center of the window.
        if (cursorVisible)
        {
            // Hide the cursor.
            setCursor(QCursor(Qt::BlankCursor));
            cursorVisible = false;

            // Save the cursor position.
            saveCursorPos = pt;

            // Compute the center point of the OpenGL Widget.
            QPoint center;
            center.setX(width() / 2);
            center.setY(height() / 2);

            // Set the cursor position to the center of the OpenGL Widget.
            x = center.rx() + (x - lastX);
            y = center.ry() + (y - lastY);
            lastX = (int) center.rx();
            lastY = (int) center.ry();

            center = mapToGlobal(center);
            QCursor::setPos(center);
        }
        else
        {
            if (x - lastX != 0 || y - lastY != 0)
                QCursor::setPos(pt);
        }
    }
    else
    {
        appCore->mouseMove(x, y);

        lastX = x;
        lastY = y;
    }
}


void CelestiaGlWidget::mousePressEvent( QMouseEvent* m )
{
    lastX = (int) m->x();
    lastY = (int) m->y();

    if (m->button() == LeftButton)
        appCore->mouseButtonDown(m->x(), m->y(), CelestiaCore::LeftButton);
    else if (m->button() == MidButton)
        appCore->mouseButtonDown(m->x(), m->y(), CelestiaCore::MiddleButton);
    else if (m->button() == RightButton)
        appCore->mouseButtonDown(m->x(), m->y(), CelestiaCore::RightButton);

}


void CelestiaGlWidget::mouseReleaseEvent( QMouseEvent* m )
{
    if (m->button() == LeftButton)
    {
        if (!cursorVisible)
        {
            // Restore the cursor position and make it visible again.
            setCursor(QCursor(Qt::CrossCursor));
            cursorVisible = true;
            QCursor::setPos(saveCursorPos);
        }
        appCore->mouseButtonUp(m->x(), m->y(), CelestiaCore::LeftButton);
    }
    else if (m->button() == MidButton)
    {
        lastX = (int) m->x();
        lastY = (int) m->y();
        appCore->mouseButtonUp(m->x(), m->y(), CelestiaCore::MiddleButton);
    }
    else if (m->button() == RightButton)
    {
        if (!cursorVisible)
        {
            // Restore the cursor position and make it visible again.
            setCursor(QCursor(Qt::CrossCursor));
            cursorVisible = true;
            QCursor::setPos(saveCursorPos);
        }
        appCore->mouseButtonUp(m->x(), m->y(), CelestiaCore::RightButton);
    }
}


void CelestiaGlWidget::wheelEvent( QWheelEvent* w )
{
    if (w->delta() > 0 )
    {
        appCore->mouseWheel(-1.0f, 0);
    }
    else if (w->delta() < 0)
    {
        appCore->mouseWheel(1.0f, 0);
    }
}


bool CelestiaGlWidget::handleSpecialKey(QKeyEvent* e, bool down)
{
    int k = -1;
    switch (e->key())
    {
    case Key_Up:
        k = CelestiaCore::Key_Up;
        break;
    case Key_Down:
        k = CelestiaCore::Key_Down;
        break;
    case Key_Left:
        k = CelestiaCore::Key_Left;
        break;
    case Key_Right:
        k = CelestiaCore::Key_Right;
        break;
    case Key_Home:
        k = CelestiaCore::Key_Home;
        break;
    case Key_End:
        k = CelestiaCore::Key_End;
        break;
    case Key_F1:
        k = CelestiaCore::Key_F1;
        break;
    case Key_F2:
        k = CelestiaCore::Key_F2;
        break;
    case Key_F3:
        k = CelestiaCore::Key_F3;
        break;
    case Key_F4:
        k = CelestiaCore::Key_F4;
        break;
    case Key_F5:
        k = CelestiaCore::Key_F5;
        break;
    case Key_F6:
        k = CelestiaCore::Key_F6;
        break;
    case Key_F7:
        k = CelestiaCore::Key_F7;
        break;
    case Key_F11:
        k = CelestiaCore::Key_F11;
        break;
    case Key_F12:
        k = CelestiaCore::Key_F12;
        break;
    case Key_PageDown:
        k = CelestiaCore::Key_PageDown;
        break;
    case Key_PageUp:
        k = CelestiaCore::Key_PageUp;
        break;
/*    case Key_F10:
        if (e->modifiers()& ShiftModifier)
            k = CelestiaCore::Key_F10;
        break;*/     
    case Key_0:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad0;
        break;
    case Key_1:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad1;
        break;
    case Key_2:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad2;
        break;
    case Key_3:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad3;
        break;
    case Key_4:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad4;
        break;
    case Key_5:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad5;
        break;
    case Key_6:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad6;
        break;
    case Key_7:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad7;
        break;
    case Key_8:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad8;
        break;
    case Key_9:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad9;
        break;
    case Qt::Key_A:
        if (e->modifiers() == NoModifier)
            k = 'A';
        break;
    case Qt::Key_Z:
        if (e->modifiers() == NoModifier)
            k = 'Z';
        break;
    }

    if (k >= 0)
    {
        int buttons = 0;
        if (e->modifiers() & ShiftModifier)
            buttons |= CelestiaCore::ShiftKey;

        if (down)
            appCore->keyDown(k, buttons);
        else
            appCore->keyUp(k);
        return (k < 'A' || k > 'Z');
    }
    else
    {
        return false;
    }
}


void CelestiaGlWidget::keyPressEvent( QKeyEvent* e )
{
    int modifiers = 0;
    if (e->modifiers() & ShiftModifier)
        modifiers |= CelestiaCore::ShiftKey;
    if (e->modifiers() & ControlModifier)
        modifiers |= CelestiaCore::ControlKey;

    switch (e->key())
    {
    case Key_Escape:
        appCore->charEntered('\033');
        break;
    case Key_Backtab:
        appCore->charEntered(CelestiaCore::Key_BackTab);
        break;
	// Support for Cyrillic keyboard
	case VK_B:
//      if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//      appCore->charEntered(e->text().toUtf8().data(), modifiers);
//      else
        //if (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete)
        //appCore->charEntered(e->text().toUtf8().data(), modifiers);
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+B]
        else
        appCore->charEntered('B');
        break;

	case VK_H:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        appCore->charEntered('H');
        break;

	case VK_D:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+D]
        else
        appCore->charEntered('D');
        break;

    case VK_C:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+C]
        else if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('C');
        else
        appCore->charEntered('c');
        break;

    case VK_G:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))  
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+G]
        else
        appCore->charEntered('G');
        break;

	case VK_F:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+F]
        else
        appCore->charEntered('F');
        break;

	case VK_T:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+T]
        else
        appCore->charEntered('T');
        break;

	case VK_Y:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+Y]
        else
        appCore->charEntered('Y');
        break;

	case VK_J:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        appCore->charEntered('J');
        break;

    case VK_L:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+L]
        else if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('L');
        else
        appCore->charEntered('l');
        break;

    case VK_K:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+K]
        else if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('K');
        else
        appCore->charEntered('k');
        break;

    case VK_E:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+E]
        else if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('E');
        else
        appCore->charEntered('e');
        break;

    case VK_P:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+P]
        else if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('P');
        else
        appCore->charEntered('p');
        break;

    case VK_R:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+R]
        else if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('R');
        else
        appCore->charEntered('r');
        break;

    case VK_M:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('M');
        else
        appCore->charEntered('m');
        break;

    case VK_W:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+W]
        else if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('W');
        else
        appCore->charEntered('w');
        break;

    case VK_N:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        appCore->charEntered('N');
        break;

    case VK_V:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+V]
        else
        appCore->charEntered('V');
        break;

    case VK_U:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+U]
        else if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('U');
        else
        appCore->charEntered('u');
        break;

    case VK_I:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        appCore->charEntered('I');
        break;

    case VK_O:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+O]
        else
        appCore->charEntered('O');
        break;

    case VK_Q:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        appCore->charEntered('Q');
        break;

    case VK_S:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers);   // [Ctrl+S]
        else
        appCore->charEntered('S');
        break;

    case VK_X:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ControlModifier))
        appCore->charEntered(e->text().toUtf8().data(), modifiers); // [Ctrl+X]
        else
        appCore->charEntered('X');
        break;

    case 1102:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        appCore->charEntered('.');
        break;

    case 1073:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        appCore->charEntered(',');
        break;

    case 1078:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered(':');
        else
        appCore->charEntered(';');
        break;

    case 1105:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('~');
        else
        appCore->charEntered('`');
        break;

    case 1093:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('{');
        else
        appCore->charEntered('[');
        break;

    case 1098:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('}');
        else
        appCore->charEntered(']');
        break;

    case 1101:
//        if ((LOWORD(GetKeyboardLayout(GetCurrentThreadId())) != 1049) || (appCore->getTextEnterMode() & CelestiaCore::KbAutoComplete))
//        appCore->charEntered(e->text().toUtf8().data(), modifiers);
//        else
        if (e->modifiers().testFlag(Qt::ShiftModifier))
        appCore->charEntered('"');
        else
        appCore->charEntered('/');
        break;
// -----------------------------------------------------------------------------

    default:
        if (!handleSpecialKey(e, true))
        {
            if ((e->text() != 0) && (e->text() != ""))
            {
                appCore->charEntered(e->text().toUtf8().data(), modifiers);
            }
        }
    }
}


void CelestiaGlWidget::keyReleaseEvent( QKeyEvent* e )
{
    handleSpecialKey(e, false);
}


void CelestiaGlWidget::setCursorShape(CelestiaCore::CursorShape shape)
{
    Qt::CursorShape cursor;
    if (currentCursor != shape)
    {
        switch(shape)
        {
        case CelestiaCore::ArrowCursor:
            cursor = Qt::ArrowCursor;
            break;
        case CelestiaCore::UpArrowCursor:
            cursor = Qt::UpArrowCursor;
            break;
        case CelestiaCore::CrossCursor:
            cursor = Qt::CrossCursor;
            break;
        case CelestiaCore::InvertedCrossCursor:
            cursor = Qt::CrossCursor;
            break;
        case CelestiaCore::WaitCursor:
            cursor = Qt::WaitCursor;
            break;
        case CelestiaCore::BusyCursor:
            cursor = Qt::WaitCursor;
            break;
        case CelestiaCore::IbeamCursor:
            cursor = Qt::IBeamCursor;
            break;
        case CelestiaCore::SizeVerCursor:
            cursor = Qt::SizeVerCursor;
            break;
        case CelestiaCore::SizeHorCursor:
            cursor = Qt::SizeHorCursor;
            break;
        case CelestiaCore::SizeBDiagCursor:
            cursor = Qt::SizeBDiagCursor;
            break;
        case CelestiaCore::SizeFDiagCursor:
            cursor = Qt::SizeFDiagCursor;
            break;
        case CelestiaCore::SizeAllCursor:
            cursor = Qt::SizeAllCursor;
            break;
        case CelestiaCore::SplitVCursor:
            cursor = Qt::SplitVCursor;
            break;
        case CelestiaCore::SplitHCursor:
            cursor = Qt::SplitHCursor;
            break;
        case CelestiaCore::PointingHandCursor:
            cursor = Qt::PointingHandCursor;
            break;
        case CelestiaCore::ForbiddenCursor:
            cursor = Qt::ForbiddenCursor;
            break;
        case CelestiaCore::WhatsThisCursor:
            cursor = Qt::WhatsThisCursor;
            break;
        default:
            cursor = Qt::CrossCursor;
            break;
        }
        setCursor(QCursor(cursor));
        currentCursor = shape;
    }
}


CelestiaCore::CursorShape CelestiaGlWidget::getCursorShape() const
{
    return currentCursor;
}


QSize CelestiaGlWidget::sizeHint() const
{
    return QSize(640, 480);
}
