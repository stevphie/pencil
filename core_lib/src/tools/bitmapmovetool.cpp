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

#include "bitmapmovetool.h"

#include <cassert>
#include <QMessageBox>

#include "pointerevent.h"
#include "editor.h"
#include "toolmanager.h"
#include "viewmanager.h"
#include "strokemanager.h"
#include "selectionmanager.h"
#include "scribblearea.h"
#include "layermanager.h"
#include "mathutils.h"

BitmapMoveTool::BitmapMoveTool(QObject* parent) : BaseTool(parent)
{
}

ToolType BitmapMoveTool::type()
{
    return MOVE;
}

void BitmapMoveTool::loadSettings()
{
    mRotationIncrement = mEditor->preference()->getInt(SETTING::ROTATION_INCREMENT);
    connect(mEditor->preference(), &PreferenceManager::optionChanged, this, &BitmapMoveTool::updateSettings);
}

QCursor BitmapMoveTool::cursor()
{
    MoveMode mode = mEditor->select()->getMoveModeForSelectionAnchor(getCurrentPoint());
    return mScribbleArea->currentTool()->selectMoveCursor(mode, type());
}

void BitmapMoveTool::updateSettings(const SETTING setting)
{
    switch (setting)
    {
    case SETTING::ROTATION_INCREMENT:
    {
        mRotationIncrement = mEditor->preference()->getInt(SETTING::ROTATION_INCREMENT);
        break;
    }
    default:
        break;

    }
}

void BitmapMoveTool::pointerPressEvent(PointerEvent* event)
{
    mCurrentLayer = currentPaintableLayer();
    if (mCurrentLayer == nullptr) return;

    mEditor->select()->updatePolygons();

    setAnchorToLastPoint();
    beginInteraction(event->modifiers());
}

void BitmapMoveTool::pointerMoveEvent(PointerEvent* event)
{
    mCurrentLayer = currentPaintableLayer();
    if (mCurrentLayer == nullptr) return;

    mEditor->select()->updatePolygons();

    if (mScribbleArea->isPointerInUse())   // the user is also pressing the mouse (dragging)
    {
        transformSelection(event->modifiers());
    }
    else
    {
        // the user is moving the mouse without pressing it
        // update cursor to reflect selection corner interaction
        mScribbleArea->updateToolCursor();
    }
    mScribbleArea->updateCurrentFrame();
}

void BitmapMoveTool::pointerReleaseEvent(PointerEvent*)
{
    auto selectMan = mEditor->select();
    if (!selectMan->somethingSelected())
        return;

    mRotatedAngle = selectMan->myRotation();
    updateTransformation();

    selectMan->updatePolygons();

    mScribbleArea->updateToolCursor();
    mScribbleArea->updateCurrentFrame();
}

void BitmapMoveTool::updateTransformation()
{
    auto selectMan = mEditor->select();
    selectMan->updateTransformedSelection();

    // make sure transform is correct
    selectMan->calculateSelectionTransformation();

    // paint the transformation
    paintTransformedSelection();
}

void BitmapMoveTool::transformSelection(Qt::KeyboardModifiers keyMod)
{
    auto selectMan = mEditor->select();
    if (selectMan->somethingSelected())
    {
        QPoint offset = offsetFromPressPos().toPoint();

        // maintain aspect ratio
        if (keyMod == Qt::ShiftModifier)
        {
            offset = selectMan->offsetFromAspectRatio(offset.x(), offset.y()).toPoint();
        }

        int rotationIncrement = 0;
        if (selectMan->getMoveMode() == MoveMode::ROTATION && keyMod & Qt::ShiftModifier)
        {
            rotationIncrement = mRotationIncrement;
        }

        selectMan->adjustSelection(getCurrentPoint(), offset.x(), offset.y(), mRotatedAngle, rotationIncrement);

        selectMan->calculateSelectionTransformation();
        paintTransformedSelection();

    }
    else // there is nothing selected
    {
        selectMan->setMoveMode(MoveMode::NONE);
    }
}

void BitmapMoveTool::beginInteraction(Qt::KeyboardModifiers keyMod)
{
    auto selectMan = mEditor->select();
    QRectF selectionRect = selectMan->myTransformedSelectionRect();
    if (!selectionRect.isNull())
    {
        mEditor->backup(typeName());
    }

    if (keyMod != Qt::ShiftModifier)
    {
        if (selectMan->isOutsideSelectionArea(getCurrentPoint()))
        {
            applyTransformation();
            mEditor->deselectAll();
        }
    }

    if (selectMan->validateMoveMode(getLastPoint()) == MoveMode::MIDDLE)
    {
        if (keyMod == Qt::ControlModifier) // --- rotation
        {
            selectMan->setMoveMode(MoveMode::ROTATION);
        }
    }

    if(selectMan->getMoveMode() == MoveMode::ROTATION) {
        QPointF curPoint = getCurrentPoint();
        QPointF anchorPoint = selectionRect.center();
        mRotatedAngle = MathUtils::radToDeg(MathUtils::getDifferenceAngle(anchorPoint, curPoint)) - selectMan->myRotation();
    }
}

void BitmapMoveTool::setAnchorToLastPoint()
{
    anchorOriginPoint = getLastPoint();
}

void BitmapMoveTool::cancelChanges()
{
    auto selectMan = mEditor->select();
    mScribbleArea->cancelTransformedSelection();
    selectMan->resetSelectionProperties();
    mEditor->deselectAll();
}

void BitmapMoveTool::applySelectionChanges()
{
    mEditor->select()->setRotation(0);
    mRotatedAngle = 0;

    mScribbleArea->applySelectionChanges();
}

void BitmapMoveTool::applyTransformation()
{
    mScribbleArea->applyTransformedSelection();
}

void BitmapMoveTool::paintTransformedSelection()
{
    mScribbleArea->paintTransformedSelection();
}

bool BitmapMoveTool::leavingThisTool()
{
    applySelectionChanges();
    return true;
}

bool BitmapMoveTool::switchingLayer()
{
    auto selectMan = mEditor->select();
    if (!selectMan->transformHasBeenModified())
    {
        mEditor->deselectAll();
        return true;
    }

    int returnValue = showTransformWarning();

    if (returnValue == QMessageBox::Yes)
    {
        applySelectionChanges();
        mEditor->deselectAll();
        return true;
    }
    else if (returnValue == QMessageBox::No)
    {
        cancelChanges();
        return true;
    }
    else if (returnValue == QMessageBox::Cancel)
    {
        return false;
    }
    return true;
}

int BitmapMoveTool::showTransformWarning()
{
    int returnValue = QMessageBox::warning(nullptr,
                                           tr("Layer switch", "Windows title of layer switch pop-up."),
                                           tr("You are about to switch away, do you want to apply the transformation?"),
                                           QMessageBox::No | QMessageBox::Cancel | QMessageBox::Yes,
                                           QMessageBox::Yes);
    return returnValue;
}

Layer* BitmapMoveTool::currentPaintableLayer()
{
    Layer* layer = mEditor->layers()->currentLayer();
    if (layer == nullptr)
        return nullptr;
    if (!layer->isPaintable())
        return nullptr;
    return layer;
}

QPointF BitmapMoveTool::offsetFromPressPos()
{
    return getCurrentPoint() - getCurrentPressPoint();
}
