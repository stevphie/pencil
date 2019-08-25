/*

Pencil - Traditional Animation Software
Copyright (C) 2005-2007 Patrick Corrieri & Pascal Naidon
Copyright (C) 2012-2018 Matthew Chiawen Chang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*/
#include "bitmaperasertool.h"

#include <QSettings>

#include "editor.h"
#include "blitrect.h"
#include "scribblearea.h"
#include "strokemanager.h"
#include "layermanager.h"
#include "viewmanager.h"
#include "pointerevent.h"


BitmapEraserTool::BitmapEraserTool(QObject* parent) : StrokeTool(parent)
{
}

ToolType BitmapEraserTool::type()
{
    return ERASER;
}

QCursor BitmapEraserTool::cursor()
{
    return Qt::CrossCursor;
}

void BitmapEraserTool::loadSettings()
{
    mPropertyEnabled[WIDTH] = true;
    mPropertyEnabled[FEATHER] = true;
    mPropertyEnabled[PRESSURE] = true;
    mPropertyEnabled[STABILIZATION] = true;
    mPropertyEnabled[ANTI_ALIASING] = true;

    QSettings settings(PENCIL2D, PENCIL2D);

    properties.width = settings.value("eraserWidth", 24.0).toDouble();
    properties.feather = settings.value("eraserFeather", 48.0).toDouble();
    properties.pressure = settings.value("eraserPressure", true).toBool();
    properties.stabilizerLevel = settings.value("stabilizerLevel", StabilizationLevel::NONE).toInt();
}

void BitmapEraserTool::resetToDefault()
{
    setWidth(24.0);
    setFeather(48.0);
    setUseFeather(false);
    setPressure(true);
    setStabilizerLevel(StabilizationLevel::NONE);
}

void BitmapEraserTool::setWidth(const qreal width)
{
    // Set current property
    properties.width = width;

    // Update settings
    QSettings settings(PENCIL2D, PENCIL2D);
    settings.setValue("eraserWidth", width);
    settings.sync();
}

void BitmapEraserTool::setFeather(const qreal feather)
{
    // Set current property
    properties.feather = feather;

    // Update settings
    QSettings settings(PENCIL2D, PENCIL2D);
    settings.setValue("eraserFeather", feather);
    settings.sync();
}

void BitmapEraserTool::setPressure(const bool pressure)
{
    // Set current property
    properties.pressure = pressure;

    // Update settings
    QSettings settings(PENCIL2D, PENCIL2D);
    settings.setValue("eraserPressure", pressure);
    settings.sync();
}

void BitmapEraserTool::setStabilizerLevel(const int level)
{
    properties.stabilizerLevel = level;

    QSettings settings(PENCIL2D, PENCIL2D);
    settings.setValue("stabilizerLevel", level);
    settings.sync();
}

void BitmapEraserTool::pointerPressEvent(PointerEvent*)
{
    mScribbleArea->setAllDirty();

    startStroke();
    mLastBrushPoint = getCurrentPoint();
    mMouseDownPoint = getCurrentPoint();
}

void BitmapEraserTool::pointerMoveEvent(PointerEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        mCurrentPressure = strokeManager()->getPressure();
        drawStroke();
        if (properties.stabilizerLevel != strokeManager()->getStabilizerLevel())
            strokeManager()->setStabilizerLevel(properties.stabilizerLevel);
    }
}

void BitmapEraserTool::pointerReleaseEvent(PointerEvent*)
{
    mEditor->backup(typeName());

    qreal distance = QLineF(getCurrentPoint(), mMouseDownPoint).length();
    if (distance < 1)
    {
        paintAt(mMouseDownPoint);
    }
    else
    {
        drawStroke();
    }
    paintStroke();
    endStroke();
}

// draw a single paint dab at the given location
void BitmapEraserTool::paintAt(QPointF point)
{
    qreal opacity = 1.0;
    mCurrentWidth = properties.width;
    if (properties.pressure == true)
    {
        opacity = strokeManager()->getPressure();
        mCurrentWidth = (mCurrentWidth + (strokeManager()->getPressure() * mCurrentWidth)) * 0.5;
    }

    qreal brushWidth = mCurrentWidth;

    BlitRect rect;

    rect.extend(point.toPoint());
    mScribbleArea->drawBrush(point,
                             brushWidth,
                             properties.feather,
                             QColor(255, 255, 255, 255),
                             opacity,
                             properties.useFeather,
                             properties.useAA);

    int rad = qRound(brushWidth) / 2 + 2;

    //continuously update buffer to update stroke behind grid.
    mScribbleArea->paintBitmapBufferRect(rect);

    mScribbleArea->refreshBitmap(rect, rad);
}

void BitmapEraserTool::drawStroke()
{
    StrokeTool::drawStroke();
    QList<QPointF> p = strokeManager()->interpolateStroke();

    for (auto & i : p)
    {
        i = mEditor->view()->mapScreenToCanvas(i);
    }

    qreal opacity = 1.0;
    mCurrentWidth = properties.width;
    if (properties.pressure)
    {
        opacity = strokeManager()->getPressure();
        mCurrentWidth = (mCurrentWidth + (strokeManager()->getPressure() * mCurrentWidth)) * 0.5;
    }

    qreal brushWidth = mCurrentWidth;
    qreal brushStep = (0.5 * brushWidth) - ((properties.feather / 100.0) * brushWidth * 0.5);
    brushStep = qMax(1.0, brushStep);

    BlitRect rect;

    QPointF a = mLastBrushPoint;
    QPointF b = getCurrentPoint();

    qreal distance = 4 * QLineF(b, a).length();
    int steps = qRound(distance) / brushStep;

    for (int i = 0; i < steps; i++)
    {
        QPointF point = mLastBrushPoint + (i + 1) * (brushStep)* (b - mLastBrushPoint) / distance;
        rect.extend(point.toPoint());
        mScribbleArea->drawBrush(point,
                                 brushWidth,
                                 properties.feather,
                                 QColor(255, 255, 255, 255),
                                 opacity,
                                 properties.useFeather,
                                 properties.useAA);

        if (i == (steps - 1))
        {
            mLastBrushPoint = point;
        }
    }

    int rad = qRound(brushWidth) / 2 + 2;

    // continuously update buffer to update stroke behind grid.
    mScribbleArea->paintBitmapBufferRect(rect);
    mScribbleArea->refreshBitmap(rect, rad);
}

void BitmapEraserTool::paintStroke()
{
    mScribbleArea->paintBitmapBuffer();
    mScribbleArea->setAllDirty();
    mScribbleArea->clearBitmapBuffer();
}
