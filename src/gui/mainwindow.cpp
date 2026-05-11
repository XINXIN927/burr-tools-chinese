/* BurrTools
 *
 * BurrTools is the legal property of its developers, whose
 * names are listed in the COPYRIGHT file, which is included
 * within the source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * 您应该已收到 GNU General Public License 的副本
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include "mainwindow.h"

#include "configuration.h"
#include "groupseditor.h"
#include "placementbrowser.h"
#include "movementbrowser.h"
#include "imageexport.h"
#include "stlexport.h"
#include "guigridtype.h"
#include "grideditor.h"
#include "gridtypegui.h"
#include "statuswindow.h"
#include "tooltabs.h"
#include "WindowWidgets.h"
#include "BlockList.h"
#include "Images.h"

#include "multilinewindow.h"
#include "assertwindow.h"
#include "togglebutton.h"
#include "voxeleditgroup.h"
#include "view3dgroup.h"
#include "statusline.h"
#include "resultviewer.h"
#include "separator.h"
#include "buttongroup.h"
#include "constraintsgroup.h"
#include "blocklistgroup.h"
#include "vectorexportwindow.h"
#include "convertwindow.h"
#include "assmimportwindow.h"

#include "LFl_Tile.h"

#include "../lib/ps3dloader.h"
#include "../lib/voxel.h"
#include "../lib/puzzle.h"
#include "../lib/problem.h"
#include "../lib/assembler.h"
#include "../lib/solvethread.h"
#include "../lib/disassembly.h"
#include "../lib/disassembler_0.h"
#include "../lib/gridtype.h"
#include "../lib/disasmtomoves.h"
#include "../lib/assembly.h"
#include "../lib/converter.h"
#include "../lib/millable.h"
#include "../lib/voxeltable.h"
#include "../lib/solution.h"

#include "../tools/gzstream.h"
#include "../tools/xml.h"

#include <FL/Fl_File_Chooser.H>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define GL_SILENCE_DEPRECATION 1
#include <FL/Fl_Color_Chooser.H>
#include <FL/Fl_Tooltip.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Pixmap.H>
#include <FL/Fl.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Value_Output.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Value_Input.H>
#include <FL/fl_ask.H>
#pragma GCC diagnostic pop

#include <cstdio>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
/* On Windows, use wide-char API to handle UTF-8 paths */
static bool fileExists(const char *n) {
  wchar_t wpath[2048];
  if (MultiByteToWideChar(CP_UTF8, 0, n, -1, wpath, 2048) == 0)
    return false;
  return _waccess(wpath, 0) == 0;
}
#else
/* returns true, if file exists, this is not the
 optimal way to do this. It would be better to open
 the directory the file is supposed to be in and look there
 but this is not really portable so this
 */
bool fileExists(const char *n) {

  FILE *f = fopen(n, "r");

  if (f) {
    fclose(f);
    return true;
  } else
    return false;
}
#endif

/* ----- auto-save / crash recovery ----- */

std::string mainWindow_c::getAutoSavePath(void) const {
#ifdef _WIN32
  const char * tmp = getenv("TEMP");
  if (!tmp) tmp = ".";
  return std::string(tmp) + "\\burrtools_autosave.xmpuzzle";
#else
  const char * tmp = getenv("TMPDIR");
  if (!tmp) tmp = "/tmp";
  return std::string(tmp) + "/burrtools_autosave.xmpuzzle";
#endif
}

void mainWindow_c::autoSave(void) {
  if (!changed) return;
  std::string path = getAutoSavePath();
  ogzstream ostr(path.c_str());
  if (ostr) {
    xmlWriter_c xml(ostr);
    puzzle->save(xml);
  }
}

void mainWindow_c::clearAutoSave(void) {
  std::string path = getAutoSavePath();
  std::remove(path.c_str());
}

void mainWindow_c::tryAutoSaveRecover(void) {
  std::string path = getAutoSavePath();
  if (!fileExists(path.c_str())) return;

  int choice = fl_choice("检测到上次程序崩溃时未保存的拼图，是否恢复？",
                          "取消", "恢复", "丢弃");
  if (choice == 0) return;   // cancel — keep the auto-save file
  if (choice == 2) {         // discard
    clearAutoSave();
    return;
  }

  // choice == 1 — recover
  char * savedFname = fname;
  fname = 0;
  bool ok = tryToLoad(path.c_str());

  if (ok) {
    // tryToLoad set fname to the auto-save path; reset it to the original
    if (fname) delete[] fname;
    fname = savedFname;
    changed = true;   // so the user is prompted to save-as
    char nm[300];
    if (fname)
      snprintf(nm, 299, "鲁班锁 - %s", fname);
    else
      snprintf(nm, 299, "鲁班锁 - 未知");
    label(nm);
  } else {
    if (fname) delete[] fname;
    fname = savedFname;
  }

  clearAutoSave();
}

static void cb_AddColor_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_AddColor(); }
void mainWindow_c::cb_AddColor(void) {

  unsigned char r, g, b;

  if (colorSelector->getSelection() == 0)
    r = g = b = 128;
  else
    puzzle->getColor(colorSelector->getSelection()-1, &r, &g, &b);

  if (fl_color_chooser("新颜色", r, g, b)) {
    puzzle->addColor(r, g, b);

    // add this color as a default to the matrix, so that
    // pieces of color x can be plces into cubes of color x
    int col = puzzle->colorNumber();
    for (unsigned int p = 0; p < puzzle->getNumberOfProblems(); p++)
      puzzle->getProblem(p)->allowPlacement(col, col);

    colorSelector->setSelection(puzzle->colorNumber());
    changed = true;
    View3D->getView()->showColors(puzzle, StatusLine->getColorMode());
    updateInterface();
  }
}

static void cb_RemoveColor_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_RemoveColor(); }
void mainWindow_c::cb_RemoveColor(void) {

  if (colorSelector->getSelection() == 0)
    fl_message("不能删除中性色，此颜色必须存在");
  else {
    changeColor(colorSelector->getSelection());
    puzzle->removeColor(colorSelector->getSelection());

    unsigned int current = colorSelector->getSelection();

    while ((current > 0) && (current > puzzle->colorNumber()))
      current--;

    colorSelector->setSelection(current);

    changed = true;
    View3D->getView()->showColors(puzzle, StatusLine->getColorMode());
    activateShape(PcSel->getSelection());
    updateInterface();
  }
}

static void cb_ChangeColor_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ChangeColor(); }
void mainWindow_c::cb_ChangeColor(void) {

  if (colorSelector->getSelection() == 0)
    fl_message("不能编辑中性色");
  else {
    unsigned char r, g, b;
    puzzle->getColor(colorSelector->getSelection()-1, &r, &g, &b);
    if (fl_color_chooser("更改颜色", r, g, b)) {
      puzzle->changeColor(colorSelector->getSelection()-1, r, g, b);
      changed = true;
      View3D->getView()->showColors(puzzle, StatusLine->getColorMode());
      updateInterface();
    }
  }
}

static void cb_NewShape_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_NewShape(); }
void mainWindow_c::cb_NewShape(void) {

  if (PcSel->getSelection() < puzzle->getNumberOfShapes()) {
    const voxel_c * v = puzzle->getShape(PcSel->getSelection());
    PcSel->setSelection(puzzle->addShape(v->getX(), v->getY(), v->getZ()));
  } else
    PcSel->setSelection(puzzle->addShape(ggt->defaultSize(), ggt->defaultSize(), ggt->defaultSize()));
  pieceEdit->setZ(0);
  updateInterface();
  StatPieceInfo(PcSel->getSelection());
  changed = true;
}

static void cb_DeleteShape_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_DeleteShape(); }
void mainWindow_c::cb_DeleteShape(void) {

  unsigned int current = PcSel->getSelection();

  if (current < puzzle->getNumberOfShapes()) {

    puzzle->removeShape(current);

    if (puzzle->getNumberOfShapes() == 0)
      current = (unsigned int)-1;
    else
      while (current >= puzzle->getNumberOfShapes())
        current--;

    activateShape(current);

    PcSel->setSelection(current);
    updateInterface();
    StatPieceInfo(PcSel->getSelection());

    changed = true;

  } else

    fl_message("未选中要删除的形状！");

}

static void cb_CopyShape_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_CopyShape(); }
void mainWindow_c::cb_CopyShape(void) {

  unsigned int current = PcSel->getSelection();

  if (current < puzzle->getNumberOfShapes()) {

    PcSel->setSelection(puzzle->addShape(puzzle->getGridType()->getVoxel(puzzle->getShape(current))));
    changed = true;

    updateInterface();
    StatPieceInfo(PcSel->getSelection());

  } else

    fl_message("未选中要复制的形状！");

}

static void cb_NameShape_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_NameShape(); }
void mainWindow_c::cb_NameShape(void) {

  if (PcSel->getSelection() < puzzle->getNumberOfShapes()) {

    const char * name = fl_input("输入形状名称", puzzle->getShape(PcSel->getSelection())->getName().c_str());

    if (name) {
      puzzle->getShape(PcSel->getSelection())->setName(name);
      changed = true;
      updateInterface();
    }
  }
}

static void cb_WeightInc_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_WeightChange(1); }
static void cb_WeightDec_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_WeightChange(-1); }
void mainWindow_c::cb_WeightChange(int by) {

  if (PcSel->getSelection() < puzzle->getNumberOfShapes()) {

    voxel_c * v = puzzle->getShape(PcSel->getSelection());
    v->setWeight(v->getWeight() + by);
    changed = true;
    updateInterface();
  }
}


static void cb_TaskSelectionTab_stub(Fl_Widget* o, void* v) { ((mainWindow_c*)v)->cb_TaskSelectionTab((Fl_Tabs*)o); }
void mainWindow_c::cb_TaskSelectionTab(Fl_Tabs* o) {

  if (o->value() == TabPieces) {
    activateShape(PcSel->getSelection());
    StatPieceInfo(PcSel->getSelection());
    if (!shapeEditorWithBig3DView)
      Small3DView();
    else
      Big3DView();
    ViewSizes[currentTab] = View3D->getZoom();
    if (ViewSizes[0] >= 0)
      View3D->setZoom(ViewSizes[0]);
    currentTab = 0;
  } else if(o->value() == TabProblems) {

    // make sure the selector has a valid problem selected, when there is one
    if (puzzle->getNumberOfProblems() && (problemSelector->getSelection() >= puzzle->getNumberOfProblems()))
      problemSelector->setSelection(puzzle->getNumberOfProblems()-1);

    if (problemSelector->getSelection() < puzzle->getNumberOfProblems()) {
      activateProblem(problemSelector->getSelection());
    }
    StatProblemInfo(problemSelector->getSelection());
    Big3DView();
    ViewSizes[currentTab] = View3D->getZoom();
    if (ViewSizes[1] >= 0)
      View3D->setZoom(ViewSizes[1]);
    currentTab = 1;
  } else if(o->value() == TabSolve) {

    // make sure the selector has a valid problem selected, when there is one
    if (puzzle->getNumberOfProblems() && (solutionProblem->getSelection() >= puzzle->getNumberOfProblems()))
      solutionProblem->setSelection(puzzle->getNumberOfProblems()-1);

    if ((solutionProblem->getSelection() < puzzle->getNumberOfProblems()) &&
        (SolutionSel->value()-1 < puzzle->getProblem(solutionProblem->getSelection())->getNumberOfSavedSolutions())) {
      activateSolution(solutionProblem->getSelection(), int(SolutionSel->value()-1));
    }
    Big3DView();
    StatusLine->setText("");
    ViewSizes[currentTab] = View3D->getZoom();
    if (ViewSizes[2] >= 0)
      View3D->setZoom(ViewSizes[2]);
    currentTab = 2;
  }

  updateInterface();
}

static void cb_TransformPiece_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_TransformPiece(); }
void mainWindow_c::cb_TransformPiece(void) {

  if (pieceTools->operationToAll()) {
    for (unsigned int i = 0; i < puzzle->getNumberOfShapes(); i++)
      changeShape(i);
  } else {
    changeShape(PcSel->getSelection());
  }

  StatPieceInfo(PcSel->getSelection());
  activateShape(PcSel->getSelection());

  changed = true;
}

static void cb_EditSym_stub(Fl_Widget* o, void* v) {
  ((mainWindow_c*)v)->cb_EditSym(((LToggleButton_c*)o)->value(), ((LToggleButton_c*)o)->ButtonVal());
}
void mainWindow_c::cb_EditSym(int onoff, int value) {
  if (onoff) {
    editSymmetries |= value;
  } else {
    editSymmetries &= ~value;
  }

  pieceEdit->editSymmetries(editSymmetries);
}

static void cb_EditChoice_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_EditChoice(); }
void mainWindow_c::cb_EditChoice(void) {
  switch(editChoice->getSelected()) {
    case 0:
      pieceEdit->editChoice(gridEditor_c::TSK_SET);
      break;
    case 1:
      pieceEdit->editChoice(gridEditor_c::TSK_VAR);
      break;
    case 2:
      pieceEdit->editChoice(gridEditor_c::TSK_RESET);
      break;
    case 3:
      pieceEdit->editChoice(gridEditor_c::TSK_COLOR);
      break;
  }
}

static void cb_EditMode_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_EditMode(); }
void mainWindow_c::cb_EditMode(void) {
  switch(editMode->getSelected()) {
    case 0:
      pieceEdit->editType(gridEditor_c::EDT_RUBBER);
      config.useRubberband(true);
      break;
    case 1:
      pieceEdit->editType(gridEditor_c::EDT_SINGLE);
      config.useRubberband(false);
      break;
  }
}

static void cb_PcSel_stub(Fl_Widget* o, void* v) { ((mainWindow_c*)v)->cb_PcSel((LBlockListGroup_c*)o); }
void mainWindow_c::cb_PcSel(LBlockListGroup_c* grp) {
  int reason = grp->getReason();

  switch(reason) {
  case PieceSelector::RS_CHANGEDSELECTION:
    activateShape(PcSel->getSelection());
    updateInterface();
    StatPieceInfo(PcSel->getSelection());
    break;
  }
}

static void cb_SolProbSel_stub(Fl_Widget* o, void* v) { ((mainWindow_c*)v)->cb_SolProbSel((LBlockListGroup_c*)o); }
void mainWindow_c::cb_SolProbSel(LBlockListGroup_c* grp) {
  int reason = grp->getReason();

  switch(reason) {
  case ProblemSelector::RS_CHANGEDSELECTION:

    unsigned int prob = solutionProblem->getSelection();

    if (prob < puzzle->getNumberOfProblems()) {

      /* check the number of solutions on this tab and lower the slider value if necessary */
      if (SolutionSel->value() > puzzle->getProblem(prob)->getNumberOfSavedSolutions())
        SolutionSel->value(puzzle->getProblem(prob)->getNumberOfSavedSolutions());

      updateInterface();
      activateSolution(prob, (int)SolutionSel->value()-1);
    }
    break;
  }
}

static void cb_ColSel_stub(Fl_Widget* o, void* v) { ((mainWindow_c*)v)->cb_ColSel((LBlockListGroup_c*)o); }
void mainWindow_c::cb_ColSel(LBlockListGroup_c* grp) {
  int reason = grp->getReason();

  switch(reason) {
  case PieceSelector::RS_CHANGEDSELECTION:
    pieceEdit->setColor(colorSelector->getSelection());
    updateInterface();
    activateShape(PcSel->getSelection());
    break;
  }
}

static void cb_ProbSel_stub(Fl_Widget* o, void* v) { ((mainWindow_c*)v)->cb_ProbSel((LBlockListGroup_c*)o); }
void mainWindow_c::cb_ProbSel(LBlockListGroup_c* grp) {
  int reason = grp->getReason();

  switch(reason) {
  case PieceSelector::RS_CHANGEDSELECTION:
    updateInterface();
    activateProblem(problemSelector->getSelection());
    StatProblemInfo(problemSelector->getSelection());
    break;
  }
}

static void cb_pieceEdit_stub(Fl_Widget* o, void* v) { ((mainWindow_c*)v)->cb_pieceEdit((VoxelEditGroup_c*)o); }
void mainWindow_c::cb_pieceEdit(VoxelEditGroup_c* o) {

  switch (o->getReason()) {
  case gridEditor_c::RS_MOUSEMOVE:
    if (o->getMouse())
      View3D->getView()->setMarker(o->getMouseX1(), o->getMouseY1(), o->getMouseX2(), o->getMouseY2(), o->getMouseZ(), editSymmetries);
    else
      View3D->getView()->hideMarker();
    break;
  case gridEditor_c::RS_CHANGESQUARE:
    View3D->getView()->showSingleShape(puzzle, PcSel->getSelection());
    StatPieceInfo(PcSel->getSelection());
    changeShape(PcSel->getSelection());
    changed = true;
    break;
  }

  View3D->redraw();
}

static void cb_NewProblem_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_NewProblem(); }
void mainWindow_c::cb_NewProblem(void) {

  unsigned int prob = puzzle->addProblem();

  for (unsigned int c = 0; c < puzzle->colorNumber(); c++)
    puzzle->getProblem(prob)->allowPlacement(c+1, c+1);

  problemSelector->setSelection(prob);

  changed = true;
  updateInterface();
  activateProblem(problemSelector->getSelection());
  StatProblemInfo(problemSelector->getSelection());
}

static void cb_DeleteProblem_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_DeleteProblem(); }
void mainWindow_c::cb_DeleteProblem(void) {

  if (problemSelector->getSelection() < puzzle->getNumberOfProblems()) {

    puzzle->removeProblem(problemSelector->getSelection());

    changed = true;

    while ((problemSelector->getSelection() >= puzzle->getNumberOfProblems()) &&
           (problemSelector->getSelection() > 0))
      problemSelector->setSelection(problemSelector->getSelection()-1);

    updateInterface();
    if (problemSelector->getSelection() < puzzle->getNumberOfProblems())
      activateProblem(problemSelector->getSelection());
    else
      activateClear();
    StatProblemInfo(problemSelector->getSelection());
  }
}

static void cb_CopyProblem_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_CopyProblem(); }
void mainWindow_c::cb_CopyProblem(void) {

  if (problemSelector->getSelection() < puzzle->getNumberOfProblems()) {

    unsigned int prob = puzzle->addProblem(puzzle->getProblem(problemSelector->getSelection()));
    problemSelector->setSelection(prob);

    changed = true;
    updateInterface();
    activateProblem(problemSelector->getSelection());
    StatProblemInfo(problemSelector->getSelection());
  }
}

static void cb_RenameProblem_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_RenameProblem(); }
void mainWindow_c::cb_RenameProblem(void) {

  if (problemSelector->getSelection() < puzzle->getNumberOfProblems()) {

    const char * name = fl_input("输入问题名称",
        puzzle->getProblem(problemSelector->getSelection())->getName().c_str());

    if (name) {

      puzzle->getProblem(problemSelector->getSelection())->setName(name);
      changed = true;
      updateInterface();
      activateProblem(problemSelector->getSelection());
    }
  }
}

static void cb_ProblemLeft_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ProblemExchange(-1); }
static void cb_ProblemRight_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ProblemExchange(+1); }
void mainWindow_c::cb_ProblemExchange(int with) {

  unsigned int current = problemSelector->getSelection();
  unsigned int other = current + with;

  if ((current < puzzle->getNumberOfProblems()) && (other < puzzle->getNumberOfProblems())) {
    puzzle->exchangeProblems(current, other);
    changed = true;
    problemSelector->setSelection(other);
  }
}

static void cb_ShapeLeft_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ShapeExchange(-1); }
static void cb_ShapeRight_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ShapeExchange(+1); }
void mainWindow_c::cb_ShapeExchange(int with) {

  unsigned int current = PcSel->getSelection();
  unsigned int other = current + with;

  if ((current < puzzle->getNumberOfShapes()) && (other < puzzle->getNumberOfShapes())) {
    puzzle->exchangeShapes(current, other);
    changed = true;
    PcSel->setSelection(other);
  }
}

static void cb_ProbShapeLeft_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ProbShapeExchange(-1); }
static void cb_ProbShapeRight_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ProbShapeExchange(+1); }
void mainWindow_c::cb_ProbShapeExchange(int with) {

  unsigned int p = problemSelector->getSelection();
  unsigned int s = shapeAssignmentSelector->getSelection();

  problem_c * pr = puzzle->getProblem(p);

  // find out the index in the problem table
  unsigned int current;

  for (current = 0; current < pr->getNumberOfParts(); current++)
    if (pr->getShapeIdOfPart(current) == s)
      break;

  unsigned int other = current + with;

  if ((current < pr->getNumberOfParts()) && (other < pr->getNumberOfParts())) {
    pr->exchangeParts(current, other);
    changed = true;
    updateInterface();
    activateProblem(problemSelector->getSelection());
  }
}

static void cb_ColorAssSel_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ColorAssSel(); }
void mainWindow_c::cb_ColorAssSel(void) {
  updateInterface();
}

static void cb_ColorConstrSel_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ColorConstrSel(); }
void mainWindow_c::cb_ColorConstrSel(void) {
  updateInterface();
}

static void cb_ShapeToResult_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ShapeToResult(); }
void mainWindow_c::cb_ShapeToResult(void) {

  if (problemSelector->getSelection() >= puzzle->getNumberOfProblems()) {
    fl_message("请先创建一个问题");
    return;
  }

  if (shapeAssignmentSelector->getSelection() >= puzzle->getNumberOfShapes())
    return;

  unsigned int prob = problemSelector->getSelection();
  problem_c * pr = puzzle->getProblem(prob);

  // check if this shape is already a piece of the problem
  pr->setShapeMaximum(shapeAssignmentSelector->getSelection(), 0);

  pr->setResultId(shapeAssignmentSelector->getSelection());
  problemResult->setPuzzle(puzzle->getProblem(prob));
  activateProblem(prob);
  StatProblemInfo(prob);
  updateInterface();

  changed = true;
}

static void cb_ShapeSel_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_SelectProblemShape(); }
void mainWindow_c::cb_SelectProblemShape(void) {
  updateInterface();
  activateProblem(problemSelector->getSelection());
}

static void cb_PiecesClicked_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_PiecesClicked(); }
void mainWindow_c::cb_PiecesClicked(void) {

  problem_c * pr = puzzle->getProblem(problemSelector->getSelection());

  shapeAssignmentSelector->setSelection(pr->getShapeIdOfPart(PiecesCountList->getClicked()));

  updateInterface();
  activateProblem(problemSelector->getSelection());
}

static void cb_AddShapeToProblem_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_AddShapeToProblem(); }
void mainWindow_c::cb_AddShapeToProblem(void) {

  if (problemSelector->getSelection() >= puzzle->getNumberOfProblems()) {
    fl_message("请先创建一个问题");
    return;
  }
  unsigned int shape = shapeAssignmentSelector->getSelection();
  if (shape >= puzzle->getNumberOfShapes())
    return;

  unsigned int prob = problemSelector->getSelection();

  changed = true;
  PiecesCountList->redraw();

  problem_c * pr = puzzle->getProblem(prob);

  // first see, if there is already a selected shape inside
  pr->setShapeMaximum(shape, pr->getShapeMaximum(shape) + 1);
  pr->setShapeMinimum(shape, pr->getShapeMinimum(shape) + 1);

  activateProblem(problemSelector->getSelection());
  updateInterface();
  StatProblemInfo(problemSelector->getSelection());
}

static void cb_AddAllShapesToProblem_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_AddAllShapesToProblem(); }
void mainWindow_c::cb_AddAllShapesToProblem(void) {

  if (problemSelector->getSelection() >= puzzle->getNumberOfProblems()) {
    fl_message("请先创建一个问题");
    return;
  }

  unsigned int prob = problemSelector->getSelection();

  changed = true;
  PiecesCountList->redraw();

  problem_c * pr = puzzle->getProblem(prob);

  for (unsigned int j = 0; j < puzzle->getNumberOfShapes(); j++) {

    // we don't add the result shape
    if (pr->resultValid() && j == pr->getResultId())
      continue;

    pr->setShapeMaximum(j, pr->getShapeMaximum(j) + 1);
    pr->setShapeMinimum(j, pr->getShapeMinimum(j) + 1);
  }

  activateProblem(problemSelector->getSelection());
  PcVis->setPuzzle(puzzle->getProblem(solutionProblem->getSelection()));
  updateInterface();
  StatProblemInfo(problemSelector->getSelection());
}

static void cb_RemoveShapeFromProblem_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_RemoveShapeFromProblem(); }
void mainWindow_c::cb_RemoveShapeFromProblem(void) {

  if (problemSelector->getSelection() >= puzzle->getNumberOfProblems()) {
    fl_message("请先创建一个问题");
    return;
  }

  unsigned int shape = shapeAssignmentSelector->getSelection();

  if (shape >= puzzle->getNumberOfShapes())
    return;

  unsigned int prob = problemSelector->getSelection();

  problem_c * pr = puzzle->getProblem(prob);

  if (pr->getShapeMinimum(shape) > 0) pr->setShapeMinimum(shape, pr->getShapeMinimum(shape)-1);
  if (pr->getShapeMaximum(shape) > 0) pr->setShapeMaximum(shape, pr->getShapeMaximum(shape)-1);

  changed = true;
  PiecesCountList->redraw();
  PcVis->setPuzzle(puzzle->getProblem(solutionProblem->getSelection()));

  activateProblem(problemSelector->getSelection());
  StatProblemInfo(problemSelector->getSelection());
}


static void cb_SetShapeMinimumToZero_stub (Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_SetShapeMinimumToZero(); }
void mainWindow_c::cb_SetShapeMinimumToZero(void) {

  if (problemSelector->getSelection() >= puzzle->getNumberOfProblems()) {
    fl_message("请先创建一个问题");
    return;
  }

  unsigned int shape = shapeAssignmentSelector->getSelection();

  if (shape >= puzzle->getNumberOfShapes())
    return;

  unsigned int prob = problemSelector->getSelection();
  changeProblem(prob);

  problem_c * pr = puzzle->getProblem(prob);

  pr->setShapeMinimum(shape, 0);

  changed = true;
  PiecesCountList->redraw();
  PcVis->setPuzzle(puzzle->getProblem(solutionProblem->getSelection()));

  activateProblem(problemSelector->getSelection());
  StatProblemInfo(problemSelector->getSelection());
}


static void cb_RemoveAllShapesFromProblem_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_RemoveAllShapesFromProblem(); }
void mainWindow_c::cb_RemoveAllShapesFromProblem(void) {

  if (problemSelector->getSelection() >= puzzle->getNumberOfProblems()) {
    fl_message("请先创建一个问题");
    return;
  }

  unsigned int prob = problemSelector->getSelection();
  changeProblem(prob);

  problem_c * pr = puzzle->getProblem(prob);

  for (unsigned int i = 0; i < puzzle->getNumberOfShapes(); i++)
    pr->setShapeMaximum(i, 0);

  changed = true;
  PiecesCountList->redraw();
  PcVis->setPuzzle(puzzle->getProblem(solutionProblem->getSelection()));

  activateProblem(problemSelector->getSelection());
  StatProblemInfo(problemSelector->getSelection());
}

static void cb_ShapeGroup_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ShapeGroup(); }
void mainWindow_c::cb_ShapeGroup(void) {

  unsigned int prob = problemSelector->getSelection();

  groupsEditor_c * groupEditWin = new groupsEditor_c(puzzle, prob);

  groupEditWin->show();

  while (groupEditWin->visible())
    Fl::wait();

  if (groupEditWin->changed()) {

    problem_c * pr = puzzle->getProblem(prob);

    /* if the user added the result shape to the problem, we inform him and
     * remove that shape again
     */
    if (pr->resultValid() && pr->getShapeMaximum(pr->getResultId()) > 0)
      pr->setShapeMaximum(pr->getResultId(), 0);

    /* as the user may have reset the counts of one shape to zero, go
     * through the list and remove entries of zero count */
    PiecesCountList->redraw();
    PcVis->setPuzzle(puzzle->getProblem(solutionProblem->getSelection()));
    changed = true;
    activateProblem(problemSelector->getSelection());
    StatProblemInfo(problemSelector->getSelection());
    updateInterface();
  }

  delete groupEditWin;
}

static void cb_BtnPlacementBrowser_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_BtnPlacementBrowser(); }
void mainWindow_c::cb_BtnPlacementBrowser(void) {

  unsigned int prob = solutionProblem->getSelection();

  if (prob >= puzzle->getNumberOfProblems())
    return;

  if (!puzzle->getProblem(prob)->getAssembler()->getPiecePlacementSupported()) {
    fl_message("抱歉，此类型拼图没有放置浏览器");
    return;
  }

  placementBrowser_c * plbr = new placementBrowser_c(puzzle->getProblem(prob));

  plbr->show();

  while (plbr->visible())
    Fl::wait();

  delete plbr;
}

static void cb_BtnMovementBrowser_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_BtnMovementBrowser(); }
void mainWindow_c::cb_BtnMovementBrowser(void) {

  unsigned int prob = solutionProblem->getSelection();

  if (prob >= puzzle->getNumberOfProblems())
    return;

  unsigned int sol = (int)SolutionSel->value()-1;

  if (sol >= puzzle->getProblem(prob)->getNumberOfSavedSolutions())
    return;

  movementBrowser_c * mvbr = new movementBrowser_c(puzzle->getProblem(prob), sol);

  mvbr->show();

  while (mvbr->visible())
    Fl::wait();

  delete mvbr;
}

static void cb_BtnAssemblerStep_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_BtnAssemblerStep(); }
void mainWindow_c::cb_BtnAssemblerStep(void) {

  bt_assert(assmThread == 0);

  assembler_c * assm = (assembler_c*)puzzle->getProblem(solutionProblem->getSelection())->getAssembler();

  bt_assert(assm);

  assm->debug_step(1);

  if (assm->getFinished() >= 1)
    puzzle->getProblem(solutionProblem->getSelection())->finishedSolving();

  updateInterface();

  View3D->getView()->showAssemblerState(puzzle->getProblem(solutionProblem->getSelection()), assm->getAssembly());
}

static void cb_AllowColor_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_AllowColor(); }
void mainWindow_c::cb_AllowColor(void) {

  if (problemSelector->getSelection() >= puzzle->getNumberOfProblems()) {
    fl_message("请先创建一个问题");
    return;
  }

  unsigned int prob = problemSelector->getSelection();
  problem_c * pr = puzzle->getProblem(prob);

  if (colconstrList->GetSortByResult())
    pr->allowPlacement(colorAssignmentSelector->getSelection()+1,
                       colconstrList->getSelection()+1);
  else
    pr->allowPlacement(colconstrList->getSelection()+1,
                       colorAssignmentSelector->getSelection()+1);
  changed = true;
  changeProblem(problemSelector->getSelection());
  updateInterface();
}

static void cb_DisallowColor_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_DisallowColor(); }
void mainWindow_c::cb_DisallowColor(void) {

  if (problemSelector->getSelection() >= puzzle->getNumberOfProblems()) {
    fl_message("请先创建一个问题");
    return;
  }

  unsigned int prob = problemSelector->getSelection();
  problem_c * pr = puzzle->getProblem(prob);

  if (colconstrList->GetSortByResult())
    pr->disallowPlacement(colorAssignmentSelector->getSelection()+1,
                              colconstrList->getSelection()+1);
  else
    pr->disallowPlacement(colconstrList->getSelection()+1,
                          colorAssignmentSelector->getSelection()+1);

  changed = true;
  changeProblem(problemSelector->getSelection());
  updateInterface();
}

static void cb_CCSortByResult_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_CCSort(1); }
static void cb_CCSortByPiece_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_CCSort(0); }
void mainWindow_c::cb_CCSort(bool byResult) {
  colconstrList->SetSortByResult(byResult);

  if (byResult) {
    BtnColSrtPc->activate();
    BtnColSrtRes->deactivate();
  } else {
    BtnColSrtPc->deactivate();
    BtnColSrtRes->activate();
  }
}

static void cb_BtnPrepare_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_BtnPrepare(); }
void mainWindow_c::cb_BtnPrepare(void) {
  cb_BtnStart(true);
}

static void cb_BtnStart_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_BtnStart(false); }
void mainWindow_c::cb_BtnStart(bool prep_only) {

  puzzle->getProblem(solutionProblem->getSelection())->removeAllSolutions();
  SolutionEmpty = true;

  for (unsigned int i = 0; i < puzzle->getNumberOfShapes(); i++)
    puzzle->getShape(i)->initHotspot();

  cb_BtnCont(prep_only);

  updateInterface();
  changed = true;
}

static void cb_BtnCont_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_BtnCont(false); }
void mainWindow_c::cb_BtnCont(bool prep_only) {

  unsigned int prob = solutionProblem->getSelection();

  if (!(ggt->getGridType()->getCapabilities() & gridType_c::CAP_ASSEMBLE)) {
    fl_message("抱歉，此空间网格尚未支持装配器！");
    return;
  }

  if (SolveDisasm->value() && !(ggt->getGridType()->getCapabilities() & gridType_c::CAP_DISASSEMBLE)) {
    fl_message("抱歉，此空间网格尚未支持拆解器！\n"
               "您必须先禁用拆解器\n");
    return;
  }

  if (prob >= puzzle->getNumberOfProblems()) {
    fl_message("请先创建一个问题");
    return;
  }

  if (!puzzle->getProblem(prob)->resultValid()) {
    fl_message("必须先定义结果形状");
    return;
  }

  bt_assert(assmThread == 0);

  int par = solveThread_c::PAR_REDUCE;
  if (KeepMirrors->value() != 0) par |= solveThread_c::PAR_KEEP_MIRROR;
  if (KeepRotations->value() != 0) par |= solveThread_c::PAR_KEEP_ROTATIONS;
  if (DropDisassemblies->value() != 0) par |= solveThread_c::PAR_DROP_DISASSEMBLIES;
  if (SolveDisasm->value() != 0) par |= solveThread_c::PAR_DISASSM;
  if (JustCount->value() != 0) par |= solveThread_c::PAR_JUST_COUNT;
  if (CompleteRotations->value() != 0) par |= solveThread_c::PAR_COMPLETE_ROTATIONS;

  assmThread = new solveThread_c(*puzzle->getProblem(prob), par);

  assmThread->setSortMethod(sortMethod->value());
  assmThread->setSolutionLimits((int)solLimit->value(), (int)solDrop->value());

  if (!assmThread->start(prep_only)) {
    fl_message("无法启动求解过程，线程创建失败，抱歉。");
    delete assmThread;
    assmThread = 0;

  } else {

    updateInterface();
    changed = true;
  }
}

static void cb_BtnStop_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_BtnStop(); }
void mainWindow_c::cb_BtnStop(void) {

  bt_assert(assmThread);

  assmThread->stop();
}

static void cb_SolutionSel_stub(Fl_Widget* o, void* v) { ((mainWindow_c*)v)->cb_SolutionSel((Fl_Value_Slider*)o); }
void mainWindow_c::cb_SolutionSel(Fl_Value_Slider* o) {
  o->take_focus();
  activateSolution(solutionProblem->getSelection(), int(o->value()-1));
  updateInterface();
}

static void cb_SolutionAnim_stub(Fl_Widget* o, void* v) { ((mainWindow_c*)v)->cb_SolutionAnim((Fl_Value_Slider*)o); }
void mainWindow_c::cb_SolutionAnim(Fl_Value_Slider* o) {
  o->take_focus();
  if (disassemble) {
    disassemble->setStep(o->value(), config.useBlendedRemoving(), true);
    View3D->getView()->updatePositions(disassemble);
  }
}

static void cb_SrtFind_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_SortSolutions(0); }
static void cb_SrtLevel_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_SortSolutions(1); }
static void cb_SrtMoves_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_SortSolutions(2); }
static void cb_SrtPieces_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_SortSolutions(3); }
void mainWindow_c::cb_SortSolutions(unsigned int by) {
  unsigned int prob = solutionProblem->getSelection();

  if (prob >= puzzle->getNumberOfProblems())
    return;

  problem_c * pr = puzzle->getProblem(prob);

  unsigned int sol = pr->getNumberOfSavedSolutions();

  if (sol < 2)
    return;

  pr->sortSolutions(by);
  activateSolution(prob, (int)SolutionSel->value()-1);
  updateInterface();
}

static void cb_DelAll_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_DeleteSolutions(0); }
static void cb_DelBefore_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_DeleteSolutions(1); }
static void cb_DelAt_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_DeleteSolutions(2); }
static void cb_DelAfter_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_DeleteSolutions(3); }
static void cb_DelDisasmless_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_DeleteSolutions(4); }
void mainWindow_c::cb_DeleteSolutions(unsigned int which) {

  unsigned int prob = solutionProblem->getSelection();

  if (prob >= puzzle->getNumberOfProblems())
    return;

  unsigned int sol = (int)SolutionSel->value()-1;

  problem_c * pr = puzzle->getProblem(prob);

  if (sol >= pr->getNumberOfSavedSolutions())
    return;

  unsigned int cnt;

  changed = true;

  switch (which) {
  case 0:
    cnt = pr->getNumberOfSavedSolutions();
    for (unsigned int i = 0; i < cnt; i++)
      pr->removeSolution(0);
    break;
  case 1:
    for (unsigned int i = 0; i < sol; i++)
      pr->removeSolution(0);
    SolutionSel->value(1);
    break;
  case 2:
    pr->removeSolution(sol);
    break;
  case 3:
    cnt = pr->getNumberOfSavedSolutions() - sol - 1;
    for (unsigned int i = 0; i < cnt; i++)
      pr->removeSolution(sol+1);
    break;
  case 4:
    cnt = pr->getNumberOfSavedSolutions();
    {
      unsigned int i = 0;
      while (i < cnt) {
        if (pr->getSavedSolution(i)->getDisassembly() || pr->getSavedSolution(i)->getDisassemblyInfo())
          i++;
        else {
          pr->removeSolution(i);
          cnt--;
          if (SolutionSel->value() > i)
            SolutionSel->value(SolutionSel->value()-1);
        }
      }
    }
    break;
  }

  if (SolutionSel->value() > pr->getNumberOfSavedSolutions())
    SolutionSel->value(pr->getNumberOfSavedSolutions());

  activateSolution(prob, (int)SolutionSel->value()-1);
  updateInterface();
}

static void cb_DelDisasm_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_DeleteDisasm(); }
void mainWindow_c::cb_DeleteDisasm(void) {
  unsigned int prob = solutionProblem->getSelection();

  if (prob >= puzzle->getNumberOfProblems())
    return;

  problem_c * pr = puzzle->getProblem(prob);

  unsigned int sol = (int)SolutionSel->value()-1;

  if (sol >= pr->getNumberOfSavedSolutions())
    return;

  pr->getSavedSolution(sol)->removeDisassembly();

  changed = true;

  activateSolution(prob, (int)SolutionSel->value()-1);
  updateInterface();
}

static void cb_DelAllDisasm_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_DeleteAllDisasm(); }
void mainWindow_c::cb_DeleteAllDisasm(void) {
  unsigned int prob = solutionProblem->getSelection();

  if (prob >= puzzle->getNumberOfProblems())
    return;

  problem_c * pr = puzzle->getProblem(prob);

  for (unsigned int i = 0; i < pr->getNumberOfSavedSolutions(); i++)
    pr->getSavedSolution(i)->removeDisassembly();

  changed = true;

  activateSolution(prob, (int)SolutionSel->value()-1);
  updateInterface();
}

static void cb_AddDisasm_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_AddDisasm(); }
void mainWindow_c::cb_AddDisasm(void) {
  unsigned int prob = solutionProblem->getSelection();

  if (prob >= puzzle->getNumberOfProblems())
    return;

  problem_c * pr = puzzle->getProblem(prob);

  unsigned int sol = (int)SolutionSel->value()-1;

  if (sol >= pr->getNumberOfSavedSolutions())
    return;

  if (!(ggt->getGridType()->getCapabilities() & gridType_c::CAP_DISASSEMBLE)) {
    fl_message("抱歉，此空间网格尚未支持拆解器！");
    return;
  }

  disassembler_c * dis = new disassembler_0_c(*pr);

  separation_c * d = dis->disassemble(pr->getSavedSolution(sol)->getAssembly());

  changed = true;

  if (d)
    pr->getSavedSolution(sol)->setDisassembly(d);

  activateSolution(prob, (int)SolutionSel->value()-1);
  updateInterface();

  delete dis;
}

static void cb_AddAllDisasm_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_AddAllDisasm(true); }
static void cb_AddMissingDisasm_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_AddAllDisasm(false); }
void mainWindow_c::cb_AddAllDisasm(bool all) {
  unsigned int prob = solutionProblem->getSelection();

  if (prob >= puzzle->getNumberOfProblems())
    return;

  if (!(ggt->getGridType()->getCapabilities() & gridType_c::CAP_DISASSEMBLE)) {
    fl_message("抱歉，此空间网格尚未支持拆解器！");
    return;
  }

  problem_c * pr = puzzle->getProblem(prob);

  changed = true;

  disassembler_c * dis = new disassembler_0_c(*pr);

  Fl_Double_Window * w = new Fl_Double_Window(20, 20, 300, 30);
  Fl_Box * b = new Fl_Box(0, 0, 300, 30);
  w->end();
  w->label("正在拆解...");
  w->set_modal();
  char txt[100];
  w->show();

  for (unsigned int sol = 0; sol < pr->getNumberOfSavedSolutions(); sol++) {

    snprintf(txt, 100, "solved %i of %i disassemblies\n", sol, pr->getNumberOfSavedSolutions());
    b->label(txt);

    Fl::wait(0);

    if (all || !pr->getSavedSolution(sol)->getDisassembly()) {

      separation_c * d = dis->disassemble(pr->getSavedSolution(sol)->getAssembly());

      if (d)
        pr->getSavedSolution(sol)->setDisassembly(d);
    }
  }

  delete dis;
  delete w;

  activateSolution(prob, (int)SolutionSel->value()-1);
  updateInterface();
}


static void cb_PcVis_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_PcVis(); }
void mainWindow_c::cb_PcVis(void) {
  View3D->getView()->updateVisibility(PcVis);
}

static void cb_Status_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_Status(); }
void mainWindow_c::cb_Status(void) {
  View3D->getView()->showColors(puzzle, StatusLine->getColorMode());
}

static void cb_3dClick_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_3dClick(); }
void mainWindow_c::cb_3dClick(void) {


  if (TaskSelectionTab->value() == TabPieces) {

    if (Fl::event_ctrl()) {
      unsigned int shape, face;
      unsigned long voxel;

      voxel_c * sh = puzzle->getShape(PcSel->getSelection());

      if (View3D->getView()->pickShape(Fl::event_x(),
            View3D->getView()->h()-Fl::event_y(),
            &shape, &voxel, &face))
        sh->setState(voxel, voxel_c::VX_EMPTY);

      View3D->getView()->showSingleShape(puzzle, PcSel->getSelection());
      StatPieceInfo(PcSel->getSelection());
      changeShape(PcSel->getSelection());
      redraw();
      changed = true;

    } else if (Fl::event_shift() || Fl::event_alt()) {

      unsigned int shape, face;
      unsigned long voxel;

      voxel_c * sh = puzzle->getShape(PcSel->getSelection());

      if (View3D->getView()->pickShape(Fl::event_x(),
            View3D->getView()->h()-Fl::event_y(),
            &shape, &voxel, &face)) {

        unsigned int x, y, z;
        if (sh->indexToXYZ(voxel, &x, &y, &z)) {

          int nx, ny, nz;

          if (sh->getNeighbor(face, 0, x, y, z, &nx, &ny, &nz)) {

            sh->resizeInclude(nx, ny, nz);

            if (Fl::event_alt())
              sh->setState(nx, ny, nz, voxel_c::VX_VARIABLE);
            else
              sh->setState(nx, ny, nz, voxel_c::VX_FILLED);

            sh->setColor(nx, ny, nz, colorSelector->getSelection());

            View3D->getView()->showSingleShape(puzzle, PcSel->getSelection());
            StatPieceInfo(PcSel->getSelection());
            changeShape(PcSel->getSelection());
            activateShape(PcSel->getSelection());
            redraw();
            changed = true;
          }
        }
      }
    }
  } else if (TaskSelectionTab->value() == TabProblems) {

    unsigned int shape;

    if (View3D->getView()->pickShape(Fl::event_x(),
        View3D->getView()->h()-Fl::event_y(),
        &shape, 0, 0)) {

      if (shape >= 2)
        shapeAssignmentSelector->setSelection(puzzle->getProblem(problemSelector->getSelection())->getShapeIdOfPart(shape-2));
    }
  } else if (TaskSelectionTab->value() == TabSolve) {
    if (Fl::event_shift()) {
      unsigned int shape;

      if (View3D->getView()->pickShape(Fl::event_x(),
            View3D->getView()->h()-Fl::event_y(),
            &shape, 0, 0)) {

        PcVis->hidePiece(shape);
        View3D->getView()->updateVisibility(PcVis);
        redraw();
      }
    }
  }
}

static void cb_New_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_New(); }
void mainWindow_c::cb_New(void) {

  if (threadStopped()) {

    if (changed)
      if (fl_choice("拼图已修改，确定吗？", "取消", "新建拼图", 0) == 0)
        return;

    gridTypeSelectorWindow_c w;
    w.show();

    while (w.visible())
      Fl::wait();

    ReplacePuzzle(new puzzle_c(w.getGridType()));

    if (fname) {
      delete [] fname;
      fname = 0;
      label("鲁班锁 - 未知");
    }

    changed = false;
    clearAutoSave();

    StatusLine->setText("");
    updateInterface();
    activateShape(0);
  }
}

static void cb_Load_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_Load(); }
void mainWindow_c::cb_Load(void) {

  if (threadStopped()) {

    if (changed)
      if (fl_choice("拼图已修改，确定吗？", "取消", "打开", 0) == 0)
        return;

    const char * f = fl_file_chooser("打开拼图", "*.xmpuzzle", "", 0);

    tryToLoad(f);
  }
}

static void cb_Load_Ps3d_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_Load_Ps3d(); }
void mainWindow_c::cb_Load_Ps3d(void) {

  if (threadStopped()) {

    if (changed)
      if (fl_choice("拼图已修改，确定吗？", "取消", "打开", 0) == 0)
        return;

    const char * f = fl_file_chooser("导入 PuzzleSolver3D 文件", "*.puz", "", 0);

    if (f) {

      std::ifstream in(f);

      puzzle_c * newPuzzle = loadPuzzlerSolver3D(&in);
      if (!newPuzzle) {
        fl_alert("无法加载拼图，抱歉！");
        return;
      }

      if (fname) delete [] fname;
      fname = new char[strlen(f)+1];
      strcpy(fname, f);

      char nm[300];
      snprintf(nm, 299, "鲁班锁 - %s", fname);
      label(nm);

      ReplacePuzzle(newPuzzle);
      updateInterface();

      TaskSelectionTab->value(TabPieces);
      activateShape(PcSel->getSelection());
      StatPieceInfo(PcSel->getSelection());

      changed = false;
    }
  }
}

static void cb_Save_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_Save(); }
void mainWindow_c::cb_Save(void) {

  if (threadStopped()) {

    if (!fname)
      cb_SaveAs();

    else {
      ogzstream ostr(fname);

      if (ostr) {
        xmlWriter_c xml(ostr);
        puzzle->save(xml);
      }

      if (!ostr)
        fl_alert("拼图未保存！！");
      else {
        changed = false;
        clearAutoSave();
      }
    }
  }
}

static void cb_Convert_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_Convert(); }
void mainWindow_c::cb_Convert(void) {

  convertWindow_c win(puzzle->getGridType()->getType());

  win.show();

  while (win.visible())
    Fl::wait();

  if (win.okSelected())
  {
    puzzle_c * p = doConvert(puzzle, win.getTargetType());

    if (p)
    {
      ReplacePuzzle(p);
      updateInterface();
      activateShape(0);
      changed = true;
    }
  }
}

class voxelTableVector_c : public voxelTable_c
{
  private:

    const std::vector<voxel_c *> *shapes;

  public:

    voxelTableVector_c(const std::vector<voxel_c *> *s) : shapes(s) {}

  protected:

    const voxel_c * findSpace(unsigned int index) const { return (*shapes)[index]; }
};

static void cb_AssembliesToShapes_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_AssembliesToShapes(); }
void mainWindow_c::cb_AssembliesToShapes(void) {

  assmImportWindow_c win(puzzle);

  win.show();

  while (win.visible())
    Fl::wait();

  if (win.okSelected())
  {
    problem_c * pr = puzzle->getProblem(win.getSrcProblem());

    std::vector<voxel_c *> sh;

    unsigned int filter = win.getFilter();

    voxelTableVector_c voxelTab(&sh);

    for (unsigned int s = 0; s < pr->getNumberOfSavedSolutions(); s++)
    {
      voxel_c * shape = pr->getSavedSolution(s)->getAssembly()->createSpace(*pr);

      if ((filter & assmImportWindow_c::dropDisconnected) && !shape->connected(0, true, voxel_c::VX_EMPTY))
      {
        delete shape;
        continue;
      }

      symmetries_t sym = shape->selfSymmetries();

      if ((filter & assmImportWindow_c::dropMirror) && shape->getGridType()->getSymmetries()->symmetryContainsMirror(sym))
      {
        delete shape;
        continue;
      }

      if ((filter & assmImportWindow_c::dropSymmetric) && !unSymmetric(sym))
      {
        delete shape;
        continue;
      }

      if ((filter & assmImportWindow_c::dropNonMillable) && !isMillable(shape))
      {
        delete shape;
        continue;
      }

      if ((filter & assmImportWindow_c::dropNonNotchable) && !isNotchable(shape))
      {
        delete shape;
        continue;
      }

      unsigned int voxels = shape->countState(voxel_c::VX_FILLED);
      if (voxels < win.getShapeMin() || voxels > win.getShapeMax())
      {
        delete shape;
        continue;
      }

      // if the user wants no identical shapes, we look up the current
      // shape in the known shapes table and drop it if we find it
      if (filter & assmImportWindow_c::dropIdentical)
      {
        if (voxelTab.getSpace(shape))
        {
          delete shape;
          continue;
        }
      }

      sh.push_back(shape);

      // we only need to add the current shape to the shape table
      // if the user wants to drop identical shapes and we use the table
      if (filter & assmImportWindow_c::dropIdentical)
      {
        voxelTab.addSpace(sh.size()-1);
      }
    }

    if (win.getAction() == assmImportWindow_c::A_ADD_NEW)
      pr = puzzle->getProblem(puzzle->addProblem());
    else if (win.getAction() == assmImportWindow_c::A_ADD_DST)
      pr = puzzle->getProblem(win.getDstProblem());

    // add the shapes to the problem of the problem tab
    for (unsigned int s = 0; s < sh.size(); s++)
    {
      int i = puzzle->addShape(sh[s]);

      if (win.getAction() == assmImportWindow_c::A_ADD_DST || win.getAction() == assmImportWindow_c::A_ADD_NEW)
      {
        pr->setShapeMaximum(i, win.getMax());
        pr->setShapeMinimum(i, win.getMin());
      }
    }

    changed = true;
    PiecesCountList->redraw();

    updateInterface();
  }
}

void mainWindow_c::cb_InsertStdShape(void) {
  const char * data = "_____________####__####_______________##____##___####__####___##____##____##__########################__##____##__########################__##____##____##___####__####___##____##_______________####__####_____________";

  voxel_c * vx = puzzle->getGridType()->getVoxel(6, 6, 6, 0);

  for (int i = 0; i < 216; i++)
    vx->set(i, data[i] == '#' ? (voxel_type)voxel_c::VX_FILLED : (voxel_type)voxel_c::VX_EMPTY);

  vx->setName("标准十字形");
  vx->initHotspot();

  unsigned int idx = puzzle->addShape(vx);
  PcSel->setSelection(idx);

  changed = true;
  updateInterface();
}

void mainWindow_c::cb_InsertRotShape(void) {
  const char * data = "_____________##_##_##_##______________##____##___###___#####__##____##____##__########################__##____##__########################__##____##____##___###___#####__##____##_______________##_##_##_##____________";

  voxel_c * vx = puzzle->getGridType()->getVoxel(6, 6, 6, 0);

  for (int i = 0; i < 216; i++)
    vx->set(i, data[i] == '#' ? (voxel_type)voxel_c::VX_FILLED : (voxel_type)voxel_c::VX_EMPTY);

  vx->setName("旋转前形状");
  vx->initHotspot();

  unsigned int idx = puzzle->addShape(vx);
  PcSel->setSelection(idx);

  changed = true;
  updateInterface();
}

/* Position mapping for 石野码 12-bit encoding (2x2x6 piece):
   1~4: (1,1,1..4)  上右1~4
   5~8: (0,1,1..4)  上左1~4
   9~10:(1,0,2..3)  下右前/后
   11~12:(0,0,2..3) 下左前/后
   LSB (bit 0) = position 1, MSB (bit 11) = position 12 */
static const int ishidaPos[12][3] = {
  {1,1,1},{1,1,2},{1,1,3},{1,1,4},
  {0,1,1},{0,1,2},{0,1,3},{0,1,4},
  {1,0,2},{1,0,3},{0,0,2},{0,0,3}
};
void mainWindow_c::cb_CreateIshidaPiece(int code) {

  // Create 2x2x6 voxel space, initially all filled
  voxel_c * vx = puzzle->getGridType()->getVoxel(2, 2, 6, (voxel_type)voxel_c::VX_FILLED);

  // Apply 石野码: hollow positions where bits are set
  for (int p = 0; p < 12; p++)
    if (code & (1 << p))
      vx->set(ishidaPos[p][0], ishidaPos[p][1], ishidaPos[p][2], (voxel_type)voxel_c::VX_EMPTY);

  char nameBuf[32];
  snprintf(nameBuf, sizeof(nameBuf), "%d", code);
  vx->setName(nameBuf);
  vx->initHotspot();

  unsigned int idx = puzzle->addShape(vx);
  PcSel->setSelection(idx);

  changed = true;
  updateInterface();
}
static void cb_SaveAs_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_SaveAs(); }
void mainWindow_c::cb_SaveAs(void) {

  if (threadStopped()) {
    const char * f = fl_file_chooser("拼图另存为", "*.xmpuzzle", "", 0);

    if (f) {

      if (!fileExists(f) || fl_choice("文件已存在，是否覆盖？", "取消", "覆盖", 0)) {

        char f2[1000];

        // check, if the last characters are ".xmpuzzle"
        if (strcmp(f + strlen(f) - strlen(".xmpuzzle"), ".xmpuzzle")) {
          snprintf(f2, 1000, "%s.xmpuzzle", f);

        } else

          snprintf(f2, 1000, "%s", f);

        ogzstream ostr(f2);

        if (ostr)
        {
          xmlWriter_c xml(ostr);
          puzzle->save(xml);
        }

        if (!ostr)
          fl_alert("拼图未保存！！！");
        else {
          changed = false;
          clearAutoSave();
        }

        if (fname) delete [] fname;
        fname = new char[strlen(f2)+1];
        strcpy(fname, f2);

        char nm[300];
        snprintf(nm, 299, "鲁班锁 - %s", fname);
        label(nm);

      } else {

        fl_message("文件未保存！\n");
      }
    }
  }
}

static void cb_Quit_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->hide(); }
void mainWindow_c::hide(void) {
  if ((!changed) || fl_choice("拼图已修改，是否退出并放弃更改？", "取消", "退出", 0)) {
    clearAutoSave();
    Fl_Double_Window::hide();
  }
}

static void cb_Config_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_Config(); }
void mainWindow_c::cb_Config(void) {
  config.dialog();
  activateConfigOptions();
}

static void cb_Comment_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_Coment(); }
void mainWindow_c::cb_Coment(void) {

  multiLineWindow_c win("编辑注释", "修改当前拼图的注释", puzzle->getComment().c_str());

  win.show();

  while (win.visible())
    Fl::wait();

  if (win.saveChanges()) {
    puzzle->setComment(win.getText());
    changed = true;
  }
}

static void cb_ImageExportVector_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ImageExportVector(); }
void mainWindow_c::cb_ImageExportVector(void) {

  vectorExportWindow_c w;

  w.show();
  while (w.visible())
    Fl::wait();

  if (!w.cancelled)
    View3D->getView()->exportToVector(w.getFileName(), w.getVectorType());
}

static void cb_ImageExport_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_ImageExport(); }
void mainWindow_c::cb_ImageExport(void) {
  imageExport_c w(puzzle);
  w.show();

  while (w.visible()) {
    w.update();
    if (w.isWorking())
      Fl::wait(0);
    else
      Fl::wait(1);
  }
}

static void cb_STLExport_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_STLExport(); }
void mainWindow_c::cb_STLExport(void) {
  stlExport_c w(puzzle);
  w.show();

  while (w.visible()) {
    Fl::wait();
  }
}

static void cb_StatusWindow_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_StatusWindow(); }
void mainWindow_c::cb_StatusWindow(void) {

  bool again;

  do {

    statusWindow_c w(puzzle);
    w.show();

    while (w.visible()) {
      Fl::wait();
    }

    again = w.getAgain();

    if (again)
      changed = true;

  } while (again);

  unsigned int current = PcSel->getSelection();

  if (puzzle->getNumberOfShapes() == 0)
    current = (unsigned int)-1;
  else
    while (current >= puzzle->getNumberOfShapes())
      current--;

  activateShape(current);

  PcSel->setSelection(current);

  updateInterface();
}

static void cb_Toggle3D_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_Toggle3D(); }
void mainWindow_c::cb_Toggle3D(void) {

  if (TaskSelectionTab->value() == TabPieces) {
    shapeEditorWithBig3DView = !shapeEditorWithBig3DView;
    if (!shapeEditorWithBig3DView)
      Small3DView();
    else
      Big3DView();
  }
}

static void cb_About_stub(Fl_Widget* /*o*/, void* v) { ((mainWindow_c*)v)->cb_About(); }
void mainWindow_c::cb_About(void) {

  fl_message("这是 BurrTools 的图形界面\n"
             "BurrTools (c) 2003-2025 作者 Andreas Röver\n"
	     "贡献者：Arne Köhn, Bryan Turner, Derek Bosch, Michael Brown\n"
             "The latest version is available at github.com/burr-tools/burr-tools\n"
             "\n"
             "本软件基于 GPL 协议分发\n"
             "您应该已收到 GNU General Public License 的副本\n"
             "随本程序一起提供 (COPYING)；如果没有，请写信至 Free Software\n"
             "Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA\n"
             "或访问 www.fsf.org\n"
             "\n"
             "本程序使用了\n"
             "- Fltk, libZ, libpng, gzstream, gl2ps\n"
             "- Fl_Table (http://3dsite.com/people/erco/Fl_Table/)\n"
             "- tr by Brian Paul (http://www.mesa3d.org/brianp/TR.html)\n"
            );
}

void mainWindow_c::StatPieceInfo(unsigned int pc) {

  if (pc < puzzle->getNumberOfShapes()) {
    char txt[100];

    unsigned int fx = puzzle->getShape(pc)->countState(voxel_c::VX_FILLED);
    unsigned int vr = puzzle->getShape(pc)->countState(voxel_c::VX_VARIABLE);

    snprintf(txt, 100, "形状 S%i 有 %i 个体素（%i 固定，%i 可变）", pc+1, fx+vr, fx, vr);
    StatusLine->setText(txt);
  }
}

void mainWindow_c::StatProblemInfo(unsigned int prob) {

  if ((prob < puzzle->getNumberOfProblems()) && (puzzle->getProblem(prob)->resultValid())) {

    problem_c * pr = puzzle->getProblem(prob);

    char txt[100];

    unsigned int cnt = 0;
    unsigned int cntMin = 0;

    for (unsigned int i = 0; i < pr->getNumberOfParts(); i++) {
      cnt += pr->getPartShape(i)->countState(voxel_c::VX_FILLED) * pr->getPartMaximum(i);
      cntMin += pr->getPartShape(i)->countState(voxel_c::VX_FILLED) * pr->getPartMinimum(i);
    }

    if (cnt == cntMin) {

      snprintf(txt, 100, "问题 P%i 结果可包含 %i-%i 个体素，部件（n=%i）包含 %i 个体素", prob+1,
          getResultShape(*pr)->countState(voxel_c::VX_FILLED),
          getResultShape(*pr)->countState(voxel_c::VX_FILLED) +
          getResultShape(*pr)->countState(voxel_c::VX_VARIABLE),
          pr->getNumberOfPieces(), cnt);

    } else {

      snprintf(txt, 100, "问题 P%i 结果可包含 %i-%i 个体素，部件（n=%i）包含 %i-%i 个体素", prob+1,
          getResultShape(*pr)->countState(voxel_c::VX_FILLED),
          getResultShape(*pr)->countState(voxel_c::VX_FILLED) +
          getResultShape(*pr)->countState(voxel_c::VX_VARIABLE),
          pr->getNumberOfPieces(), cntMin, cnt);
    }

    StatusLine->setText(txt);

  } else

    StatusLine->setText("");
}

void mainWindow_c::changeColor(unsigned int nr) {

  for (unsigned int i = 0; i < puzzle->getNumberOfShapes(); i++)
    for (unsigned int j = 0; j < puzzle->getShape(i)->getXYZ(); j++)
      if (puzzle->getShape(i)->getColor(j) == nr) {
        changeShape(i);
        break;
      }
}

void mainWindow_c::changeShape(unsigned int nr) {
  for (unsigned int i = 0; i < puzzle->getNumberOfProblems(); i++)
    if (puzzle->getProblem(i)->usesShape(nr))
      puzzle->getProblem(i)->removeAllSolutions();
}

void mainWindow_c::changeProblem(unsigned int nr) {
  puzzle->getProblem(nr)->removeAllSolutions();
}

bool mainWindow_c::threadStopped(void) {

  if (assmThread) {

    fl_message("请先停止求解过程！");
    return false;
  }

  return true;
}

bool mainWindow_c::tryToLoad(const char * f) {

  // it may well be that the file doesn't exist, if it came from the command line
  if (!f) return false;
  if (!fileExists(f)) return false;

  std::istream * str = openGzFile(f);
  xmlParser_c pars(*str);

  puzzle_c * newPuzzle;

  try {
    newPuzzle = new puzzle_c(pars);
  }

  catch (xmlParserException_c &e)
  {
    fl_message("%s",(std::string("加载错误：") + e.what()).c_str());
    delete str;
    return false;
  }

  delete str;

  if (fname) delete [] fname;
  fname = new char[strlen(f)+1];
  strcpy(fname, f);

  char nm[300];
  snprintf(nm, 299, "鲁班锁 - %s", fname);
  label(nm);

  ReplacePuzzle(newPuzzle);
  updateInterface();

  TaskSelectionTab->value(TabPieces);
  activateShape(PcSel->getSelection());
  StatPieceInfo(PcSel->getSelection());
  View3D->getView()->showColors(puzzle, StatusLine->getColorMode());

  changed = false;
  clearAutoSave();

  // check for a started assemblies, and warn user about it
  bool containsStarted = false;

  for (unsigned int p = 0; p < puzzle->getNumberOfProblems(); p++) {
    if (puzzle->getProblem(p)->getSolveState() == SS_SOLVING) {
      containsStarted = true;
      break;
    }
  }

  if (containsStarted)
    fl_message("此拼图文件包含已启动但未完成的求解搜索。");

  if (puzzle->getCommentPopup())
    fl_message("%s",puzzle->getComment().c_str());

  return true;
}

void mainWindow_c::ReplacePuzzle(puzzle_c * NewPuzzle) {

  // inform everybody
  colorSelector->setPuzzle(NewPuzzle);
  PcSel->setPuzzle(NewPuzzle);
  pieceEdit->setPuzzle(NewPuzzle, 0);
  problemSelector->setPuzzle(NewPuzzle);
  colorAssignmentSelector->setPuzzle(NewPuzzle);
  colconstrList->setPuzzle(NewPuzzle, 0);
  if (NewPuzzle->getNumberOfProblems() > 0) {
    problemResult->setPuzzle(NewPuzzle->getProblem(0));
    PiecesCountList->setPuzzle(NewPuzzle->getProblem(0));
    PcVis->setPuzzle(NewPuzzle->getProblem(0));
  } else {
    problemResult->setPuzzle(0);
    PiecesCountList->setPuzzle(0);
    PcVis->setPuzzle(0);
  }
  shapeAssignmentSelector->setPuzzle(NewPuzzle);
  solutionProblem->setPuzzle(NewPuzzle);

  SolutionSel->value(1);
  SolutionAnim->value(0);

  if (NewPuzzle != puzzle) {
    delete puzzle;
    puzzle = NewPuzzle;
  }

  guiGridType_c * nggt = new guiGridType_c(puzzle->getGridType());

  // now replace all gridtype dependent gui elements with
  // instances from the guigridtype
  pieceEdit->newGridType(nggt, puzzle);
  pieceTools->newGridType(nggt);

  // for the pieceEditor we need to reset all the edit mode fields
  pieceEdit->editSymmetries(editSymmetries);
  switch(editChoice->getSelected()) {
    case 0: pieceEdit->editChoice(gridEditor_c::TSK_SET); break;
    case 1: pieceEdit->editChoice(gridEditor_c::TSK_VAR); break;
    case 2: pieceEdit->editChoice(gridEditor_c::TSK_RESET); break;
    case 3: pieceEdit->editChoice(gridEditor_c::TSK_COLOR); break;
  }
  switch(editMode->getSelected()) {
    case 0: pieceEdit->editType(gridEditor_c::EDT_RUBBER); break;
    case 1: pieceEdit->editType(gridEditor_c::EDT_SINGLE); break;
  }

  delete ggt;
  ggt = nggt;
}

Fl_Menu_Item mainWindow_c::menu_MainMenu[] = {
  { "&文件",           0, 0, 0, FL_SUBMENU },
    {"新建",            0, cb_New_stub,         0, 0, 0, 0, 14, 56},
    {"打开",    FL_F + 3, cb_Load_stub,        0, 0, 0, 0, 14, 56},
    {"导入",         0, cb_Load_Ps3d_stub,   0, 0, 0, 0, 14, 56},
    {"保存",    FL_F + 2, cb_Save_stub,        0, 0, 0, 0, 14, 56},
    {"另存为",        0, cb_SaveAs_stub,      0, FL_MENU_DIVIDER, 0, 0, 14, 56},
    {"转换",        0, cb_Convert_stub,     0, 0, 0, 0, 14, 56},
    {"导入装配体",   0, cb_AssembliesToShapes_stub,     0, 0, 0, 0, 14, 56},
    {"退出",           0, cb_Quit_stub,        0, 0, 3, 0, 14, 56},
    { 0 },
  {"切换3D", FL_F + 4, cb_Toggle3D_stub,    0, 0, 0, 0, 14, 56},
  { "&导出",         0, 0, 0, FL_SUBMENU },
    {"图片",             0, cb_ImageExport_stub, 0, 0, 0, 0, 14, 56},
    {"矢量图",       0, cb_ImageExportVector_stub, 0, 0, 0, 0, 14, 56},
    {"STL",             0, cb_STLExport_stub, 0, 0, 0, 0, 14, 56},
    { 0 },
  {"状态",           0, cb_StatusWindow_stub,  0, 0, 0, 0, 14, 56},
  {"编辑注释",     0, cb_Comment_stub,     0, 0, 0, 0, 14, 56},
  {"配置",           0, cb_Config_stub,      0, 0, 0, 0, 14, 56},
  {"关于",            0, cb_About_stub,       0, 0, 3, 0, 14, 56},
  {0}
};

void mainWindow_c::show(int argn, char ** argv) {
  LFl_Double_Window::show();

  int arg = 1;

  // try all command line switches, until either they are
  // all tried or one got loaded successfully
  while (arg < argn)
    if (tryToLoad(argv[arg]))
      break;
    else
      arg++;

  tryAutoSaveRecover();
}

void mainWindow_c::activateClear(void) {
  View3D->getView()->showNothing();
  pieceEdit->clearPuzzle();
  pieceTools->setVoxelSpace(0, 0);

  SolutionEmpty = true;
}

void mainWindow_c::activateShape(unsigned int number) {

  if ((number < puzzle->getNumberOfShapes())) {

    View3D->getView()->showSingleShape(puzzle, number);
    pieceEdit->setPuzzle(puzzle, number);
    pieceTools->setVoxelSpace(puzzle, number);

    PcSel->setSelection(number);

  } else {

    View3D->getView()->showNothing();
    pieceEdit->clearPuzzle();
    pieceTools->setVoxelSpace(0, 0);
  }

  SolutionEmpty = true;
}

void mainWindow_c::activateProblem(unsigned int prob) {

  if (prob < puzzle->getNumberOfProblems())
    View3D->getView()->showProblem(puzzle, prob, shapeAssignmentSelector->getSelection());
  else
    View3D->getView()->showNothing();

  SolutionEmpty = true;
}

void mainWindow_c::activateSolution(unsigned int prob, unsigned int num) {

  if (disassemble) {
    delete disassemble;
    disassemble = 0;
  }

  if ((prob < puzzle->getNumberOfProblems()) && (num < puzzle->getProblem(prob)->getNumberOfSavedSolutions())) {

    problem_c * pr = puzzle->getProblem(prob);

    PcVis->setPuzzle(puzzle->getProblem(prob));
    PcVis->setAssembly(pr->getSavedSolution(num)->getAssembly());
    AssemblyNumber->show();
    AssemblyNumber->value(pr->getSavedSolution(num)->getAssemblyNumber()+1);

    if (pr->getSavedSolution(num)->getDisassembly()) {
      SolutionAnim->show();
      SolutionAnim->range(0, pr->getSavedSolution(num)->getDisassembly()->sumMoves());

      SolutionsInfo->show();

      MovesInfo->show();

      char levelText[50];
      int len = snprintf(levelText, 50, "%i (", pr->getSavedSolution(num)->getDisassembly()->sumMoves());
      pr->getSavedSolution(num)->getDisassembly()->movesText(levelText + len, 50-len);
      levelText[strlen(levelText)+1] = 0;
      levelText[strlen(levelText)] = ')';

      MovesInfo->value(levelText);

      disassemble = new disasmToMoves_c(pr->getSavedSolution(num)->getDisassembly(),
                                      2*getResultShape(*pr)->getBiggestDimension(),
                                      pr->getNumberOfPieces());
      disassemble->setStep(SolutionAnim->value(), config.useBlendedRemoving(), true);

      if (prob < puzzle->getNumberOfProblems()) View3D->getView()->showAssembly(puzzle->getProblem(prob), num);
      View3D->getView()->updatePositions(disassemble);
      View3D->getView()->updateVisibility(PcVis);

      SolutionNumber->show();
      SolutionNumber->value(pr->getSavedSolution(num)->getSolutionNumber()+1);

    } else if (pr->getSavedSolution(num)->getDisassemblyInfo()) {

      SolutionAnim->range(0, 0);
      SolutionAnim->hide();

      SolutionsInfo->show();

      MovesInfo->show();

      char levelText[50];
      int len = snprintf(levelText, 50, "%i (", pr->getSavedSolution(num)->getDisassemblyInfo()->sumMoves());
      pr->getSavedSolution(num)->getDisassemblyInfo()->movesText(levelText + len, 50-len);
      levelText[strlen(levelText)+1] = 0;
      levelText[strlen(levelText)] = ')';

      MovesInfo->value(levelText);

      if (prob < puzzle->getNumberOfProblems()) View3D->getView()->showAssembly(puzzle->getProblem(prob), num);
      View3D->getView()->updateVisibility(PcVis);

      SolutionNumber->show();
      SolutionNumber->value(pr->getSavedSolution(num)->getSolutionNumber()+1);

    } else {

      SolutionAnim->range(0, 0);
      SolutionAnim->hide();
      MovesInfo->value(0);
      MovesInfo->hide();

      if (prob < puzzle->getNumberOfProblems()) View3D->getView()->showAssembly(puzzle->getProblem(prob), num);
      View3D->getView()->updateVisibility(PcVis);

      SolutionNumber->hide();
    }

    SolutionEmpty = false;

  } else {

    View3D->getView()->showNothing();
    SolutionEmpty = true;

    SolutionAnim->hide();
    MovesInfo->hide();

    PcVis->setPuzzle(0);

    AssemblyNumber->hide();
    SolutionNumber->hide();
  }
}

const char * timeToString(float time) {

  static char tmp[50];

  if (time < 60)                               snprintf(tmp, 50, "%i seconds",     int(time/(1                 )));
  else if (time < 60*60)                       snprintf(tmp, 50, "%.1f minutes",   time/(60                    ));
  else if (time < 60*60*24)                    snprintf(tmp, 50, "%.1f hours",     time/(60*60                 ));
  else if (time < 60*60*24*30)                 snprintf(tmp, 50, "%.1f days",      time/(60*60*24              ));
  else if (time < 60*60*24*365.2422)           snprintf(tmp, 50, "%.1f months",    time/(60*60*24*30           ));
  else if (time < 60*60*24*365.2422*1000)      snprintf(tmp, 50, "%.1f years",     time/(60*60*24*365.2422     ));
  else if (time < 60*60*24*365.2422*1000*1000) snprintf(tmp, 50, "%.1f millennia", time/(60*60*24*365.2422*1000));
  else                                         snprintf(tmp, 50, "很久");

  return tmp;
}

int mainWindow_c::findMenuEntry(const char * txt) {

  int found = -1;

  for (unsigned int i = 0; i < (sizeof(menu_MainMenu) / sizeof(menu_MainMenu[0])); i++)
    if (menu_MainMenu[i].text && (strcmp(menu_MainMenu[i].label(), txt) == 0)) {
      bt_assert(found == -1);
      found = i;
    }

  bt_assert(found >= 0);
  return found;
}

void mainWindow_c::updateInterface(void) {

  // update the menu items activate state

  // there must be at least one shape before there is something to export...
  if (puzzle->getNumberOfShapes() > 0)
    menu_MainMenu[findMenuEntry("图片")].activate();
  else
    menu_MainMenu[findMenuEntry("图片")].deactivate();

  if (ggt->getGridType()->getCapabilities() & gridType_c::CAP_STLEXPORT &&
      puzzle->getNumberOfShapes() > 0)
    menu_MainMenu[findMenuEntry("STL")].activate();
  else
    menu_MainMenu[findMenuEntry("STL")].deactivate();

  MainMenu->copy(menu_MainMenu, this);

  unsigned int prob = solutionProblem->getSelection();

  if (TaskSelectionTab->value() == TabPieces) {
    // shapes tab

    // we can only delete colours, when something valid is selected
    // and no assembler is running
    if ((colorSelector->getSelection() > 0) && !assmThread)
      BtnDelColor->activate();
    else
      BtnDelColor->deactivate();

    // colours can be changed for all colours except the neutral colour
    if (colorSelector->getSelection() > 0)
      BtnChnColor->activate();
    else
      BtnChnColor->deactivate();

    // we can only edit and copy shapes, when something valid is selected
    if (PcSel->getSelection() < puzzle->getNumberOfShapes()) {
      BtnCpyShape->activate();
      BtnRenShape->activate();
      pieceEdit->activate();
    } else {
      BtnCpyShape->deactivate();
      BtnRenShape->deactivate();
      pieceEdit->deactivate();
    }

    // shapes can only be moved, when the neighbour shape is there
    if ((PcSel->getSelection() > 0) && (PcSel->getSelection() < puzzle->getNumberOfShapes()) && !assmThread)
      BtnShapeLeft->activate();
    else
      BtnShapeLeft->deactivate();
    if ((PcSel->getSelection()+1 < puzzle->getNumberOfShapes()) && !assmThread)
      BtnShapeRight->activate();
    else
      BtnShapeRight->deactivate();

    // we can only delete shapes, when something valid is selected
    // and no assembler is running
    if ((PcSel->getSelection() < puzzle->getNumberOfShapes()) && !assmThread) {
      BtnDelShape->activate();
    } else {
      BtnDelShape->deactivate();
    }

    const problem_c * pr = (assmThread) ? &assmThread->getProblem() : 0;

    // we can only edit shapes, when something valid is selected and
    // either no assembler is running or the shape is not in the problem that the assembler works on
    if ((PcSel->getSelection() < puzzle->getNumberOfShapes()) &&
        (!assmThread || !pr->usesShape(PcSel->getSelection()))) {
      pieceTools->activate();
    } else {
      pieceTools->deactivate();
    }

    // when the current shape is in the assembler we lock the editor, only viewing is possible
    if (assmThread && (pr->usesShape(PcSel->getSelection())))
      pieceEdit->deactivate();
    else
      pieceEdit->activate();

    if (puzzle->colorNumber() < 63)
      BtnNewColor->activate();
    else
      BtnNewColor->deactivate();

  } else if (TaskSelectionTab->value() == TabProblems) {

    problem_c * pr = (problemSelector->getSelection() < puzzle->getNumberOfProblems())
      ? puzzle->getProblem(problemSelector->getSelection()) : 0;

    // problem tab
    PiecesCountList->setPuzzle(pr);
    colconstrList->setPuzzle(puzzle, problemSelector->getSelection());
    problemResult->setPuzzle(pr);

    // problems can only be renames and copied, when something valid is selected
    if (problemSelector->getSelection() < puzzle->getNumberOfProblems()) {
      BtnCpyProb->activate();
      BtnRenProb->activate();
    } else {
      BtnCpyProb->deactivate();
      BtnRenProb->deactivate();
    }

    // problems can only be shifted around when the corresponding neighbour is
    // available
    if ((problemSelector->getSelection() > 0) && (problemSelector->getSelection() < puzzle->getNumberOfProblems()) && !assmThread)
      BtnProbLeft->activate();
    else
      BtnProbLeft->deactivate();
    if ((problemSelector->getSelection()+1 < puzzle->getNumberOfProblems()) && !assmThread)
      BtnProbRight->activate();
    else
      BtnProbRight->deactivate();

    if (problemSelector->getSelection() < puzzle->getNumberOfProblems() && !assmThread)
    {
      unsigned int current;
      unsigned int p = problemSelector->getSelection();
      unsigned int s = shapeAssignmentSelector->getSelection();

      problem_c * pr = puzzle->getProblem(p);

      for (current = 0; current < pr->getNumberOfParts(); current++)
        if (pr->getShapeIdOfPart(current) == s)
          break;

      if (current && (current < pr->getNumberOfParts()))
        BtnProbShapeLeft->activate();
      else
        BtnProbShapeLeft->deactivate();
      if (current+1 < pr->getNumberOfParts())
        BtnProbShapeRight->activate();
      else
        BtnProbShapeRight->deactivate();

    } else {
      BtnProbShapeRight->deactivate();
      BtnProbShapeLeft->deactivate();
    }

    // problems can only be deleted, something valid is selected and the
    // assembler is not running
    if ((problemSelector->getSelection() < puzzle->getNumberOfProblems()) && !assmThread)
      BtnDelProb->activate();
    else
      BtnDelProb->deactivate();

    // we can only edit colour constraints when a valid problem is selected
    // the selected colour is valid
    // the assembler is not running or not busy with the selected problem
    if ((problemSelector->getSelection() < puzzle->getNumberOfProblems()) &&
        (colorAssignmentSelector->getSelection() < puzzle->colorNumber()) &&
        (!assmThread || (&(assmThread->getProblem()) != puzzle->getProblem(problemSelector->getSelection())))) {

      problem_c * pr = puzzle->getProblem(problemSelector->getSelection());

      // check, if the given colour is already added
      if (colconstrList->GetSortByResult()) {
        if (pr->placementAllowed(colorAssignmentSelector->getSelection()+1,
                                 colconstrList->getSelection()+1)) {
          BtnColAdd->deactivate();
          BtnColRem->activate();
        } else {
          BtnColAdd->activate();
          BtnColRem->deactivate();
        }
      } else {
        if (pr->placementAllowed(colconstrList->getSelection()+1,
                                 colorAssignmentSelector->getSelection()+1)) {
          BtnColAdd->deactivate();
          BtnColRem->activate();
        } else {
          BtnColAdd->activate();
          BtnColRem->deactivate();
        }
      }
    } else {
      BtnColAdd->deactivate();
      BtnColRem->deactivate();
    }

    // we can only change shapes, when a valid problem is selected
    // a valid shape is selected
    // the assembler is not running or not busy with out problem
    if ((problemSelector->getSelection() < puzzle->getNumberOfProblems()) &&
        (shapeAssignmentSelector->getSelection() < puzzle->getNumberOfShapes()) &&
        (!assmThread || (&(assmThread->getProblem()) != puzzle->getProblem(problemSelector->getSelection())))) {
      BtnSetResult->activate();

      problem_c * pr = puzzle->getProblem(problemSelector->getSelection());

      // we can only add a shape, when it's not the result of the current problem
      if (!pr->resultValid() || pr->getResultId() != shapeAssignmentSelector->getSelection())
        BtnAddShape->activate();
      else
        BtnAddShape->deactivate();

      bool found = false;

      for (unsigned int p = 0; p < pr->getNumberOfParts(); p++)
        if (pr->getShapeIdOfPart(p) == shapeAssignmentSelector->getSelection()) {
          found = true;
          break;
        }

      if (found) {
        BtnRemShape->activate();
        BtnMinZero->activate();
      } else {
        BtnRemShape->deactivate();
        BtnMinZero->deactivate();
      }

    } else {
      BtnSetResult->deactivate();
      BtnAddShape->deactivate();
      BtnRemShape->deactivate();
      BtnMinZero->deactivate();
    }

    // we can edit the groups, when we have a problem with at least one shape and
    // the assembler is not working on the current problem
    if ((problemSelector->getSelection() < puzzle->getNumberOfProblems()) &&
        (!assmThread || (&(assmThread->getProblem()) != puzzle->getProblem(problemSelector->getSelection())))) {
      BtnGroup->activate();
    } else {
      BtnGroup->deactivate();
    }

    if ((problemSelector->getSelection() < puzzle->getNumberOfProblems()) &&
        (!assmThread || (&(assmThread->getProblem()) != puzzle->getProblem(problemSelector->getSelection())))) {
      BtnAddAll->activate();
      BtnRemAll->activate();
    } else {
      BtnAddAll->deactivate();
      BtnRemAll->deactivate();
    }

  } else {

    float finished = ((prob < puzzle->getNumberOfProblems()) &&
        puzzle->getProblem(prob)->getAssembler())
          ? puzzle->getProblem(prob)->getAssembler()->getFinished()
          : 0;

    if (prob < puzzle->getNumberOfProblems()) {

      problem_c * pr = puzzle->getProblem(prob);

      // solution tab
      PcVis->setPuzzle(pr);

      // we have a valid problem selected, so update the information visible

      SolvingProgress->value(100*finished);
      SolvingProgress->show();

      {
        static char tmp[100];
        snprintf(tmp, 100, "%.4f%%", 100*finished);
        SolvingProgress->label(tmp);
      }

      unsigned long numSol = pr->getNumberOfSavedSolutions();

      if (numSol > 0) {

        SolutionSel->show();
        SolutionsInfo->show();

        SolutionSel->range(1, numSol);
        if (SolutionSel->value() > numSol)
          SolutionSel->value(numSol);
        SolutionsInfo->value(numSol);

        // if we are in the solve tab and have a valid solution
        // we can activate that
        if (SolutionEmpty && (numSol > 0)) {
          activateSolution(prob, 0);
          SolutionSel->value(1);
        }

      } else {

        SolutionSel->range(1, 1);
        SolutionSel->hide();
        SolutionsInfo->hide();
        SolutionAnim->hide();
        MovesInfo->hide();

        AssemblyNumber->hide();
        SolutionNumber->hide();
      }

      if (pr->numAssembliesKnown()) {
        OutputAssemblies->value(pr->getNumAssemblies());
        OutputAssemblies->show();
      } else {
        OutputAssemblies->hide();
      }

      if (pr->numSolutionsKnown()) {
        OutputSolutions->value(pr->getNumSolutions());
        OutputSolutions->show();
      } else {
        OutputSolutions->hide();
      }

      // the placement browser can only be activated when an assembler is available and not assembling is active
      if (pr->getAssembler() && !assmThread) {
        BtnPlacement->activate();
      } else {
        BtnPlacement->deactivate();
      }

      // the step button is only active when the placements browser can be active AND when the puzzle is not
      // yet completely solved
      if (pr->getAssembler() && !assmThread && (pr->getSolveState() != SS_SOLVED)) {
        if (BtnStep) BtnStep->activate();
      } else {
        if (BtnStep) BtnStep->deactivate();
      }

      if (pr->getNumberOfSavedSolutions() >= 2) {
        BtnSrtFind->activate();
        BtnSrtLevel->activate();
        BtnSrtMoves->activate();
        BtnSrtPieces->activate();
      } else {
        BtnSrtFind->deactivate();
        BtnSrtLevel->deactivate();
        BtnSrtMoves->deactivate();
        BtnSrtPieces->deactivate();
      }

      if (pr->getNumberOfSavedSolutions() > 0) {
        BtnDelAll->activate();
        if (SolutionSel->value() > 1)
          BtnDelBefore->activate();
        else
          BtnDelBefore->deactivate();

        BtnDelAt->activate();

        if ((SolutionSel->value()-1) < (pr->getNumberOfSavedSolutions()-1))
          BtnDelAfter->activate();
        else
          BtnDelAfter->deactivate();

        BtnDelDisasm->activate();
      } else {
        BtnDelAll->deactivate();
        BtnDelBefore->deactivate();
        BtnDelAt->deactivate();
        BtnDelAfter->deactivate();
        BtnDelDisasm->deactivate();
      }

      if ((SolutionSel->value() >= 1) &&
          ((int)SolutionSel->value()-1) < (int)pr->getNumberOfSavedSolutions() &&
          pr->getSavedSolution((int)SolutionSel->value()-1)->getDisassembly()) {
        BtnDisasmDel->activate();
      } else {
        BtnDisasmDel->deactivate();
      }

      if (pr->getNumberOfSavedSolutions() > 0) {
        BtnDisasmDelAll->activate();
      } else {
        BtnDisasmDelAll->deactivate();
      }

      if (ggt->getGridType()->getCapabilities() & gridType_c::CAP_DISASSEMBLE) {

        if (pr->getNumberOfSavedSolutions() > 0) {
          BtnDisasmAdd->activate();
          BtnDisasmAddAll->activate();
          BtnDisasmAddMissing->activate();
        } else {
          BtnDisasmAdd->deactivate();
          BtnDisasmAddAll->deactivate();
          BtnDisasmAddMissing->deactivate();
        }
        SolveDisasm->activate();
      } else {
        BtnDisasmAdd->deactivate();
        BtnDisasmAddAll->deactivate();
        BtnDisasmAddMissing->deactivate();
        SolveDisasm->deactivate();
        SolveDisasm->value(0);
      }

      if (ggt->getGridType()->getCapabilities() & gridType_c::CAP_DISASSEMBLE &&
          !assmThread &&
          solutionProblem->getSelection() < puzzle->getNumberOfProblems() &&
          SolutionSel->value()-1 < puzzle->getProblem(solutionProblem->getSelection())->getNumberOfSavedSolutions()
         )
      {
        BtnMovement->activate();
      }
      else
      {
        BtnMovement->deactivate();
      }
    } else {

      // no valid problem available, hide all information

      SolutionSel->hide();
      SolutionsInfo->hide();
      OutputSolutions->hide();
      SolutionAnim->hide();
      MovesInfo->hide();

      AssemblyNumber->hide();
      SolutionNumber->hide();

      SolvingProgress->hide();
      OutputAssemblies->hide();

      BtnPlacement->deactivate();
      BtnMovement->deactivate();
      if (BtnStep) BtnStep->deactivate();
      if (BtnPrepare) BtnPrepare->deactivate();

      BtnSrtFind->deactivate();
      BtnSrtLevel->deactivate();
      BtnSrtMoves->deactivate();
      BtnSrtPieces->deactivate();
      BtnDelAll->deactivate();
      BtnDelBefore->deactivate();
      BtnDelAt->deactivate();
      BtnDelAfter->deactivate();
      BtnDelDisasm->deactivate();
      BtnDisasmDel->deactivate();
      BtnDisasmDelAll->deactivate();
      BtnDisasmAdd->deactivate();
      BtnDisasmAddAll->deactivate();
      BtnDisasmAddMissing->deactivate();

      PcVis->setPuzzle(0);
    }


    if (assmThread && (&(assmThread->getProblem()) == puzzle->getProblem(prob))) {

      problem_c * pr = puzzle->getProblem(prob);

      // a thread is currently running

      unsigned int ut;
      if (pr->usedTimeKnown())
        ut = pr->getUsedTime() + assmThread->getTime();
      else
        ut = assmThread->getTime();

      TimeUsed->value(timeToString(ut));
      if (finished != 0)
        TimeEst->value(timeToString(ut/finished-ut));
      else
        TimeEst->value("未知");

      TimeUsed->show();
      TimeEst->show();

    } else {

      if ((prob < puzzle->getNumberOfProblems()) && puzzle->getProblem(prob)->usedTimeKnown()) {
        problem_c * pr = puzzle->getProblem(prob);
        TimeUsed->value(timeToString(pr->getUsedTime()));
        TimeUsed->show();
      } else {
        TimeUsed->hide();
      }

      TimeEst->hide();
    }

    if (assmThread) {

      problem_c * pr = puzzle->getProblem(prob);

      switch(assmThread->currentAction()) {
      case solveThread_c::ACT_PREPARATION:
        {
          char tmp[20];
          snprintf(tmp, 20, "准备部件 %i", assmThread->currentActionParameter()+1);
          OutputActivity->value(tmp);
        }
        break;
      case solveThread_c::ACT_REDUCE:
        if (pr->getAssembler()) {
          char tmp[20];
          snprintf(tmp, 20, "优化部件 %i", pr->getAssembler()->getReducePiece()+1);
          OutputActivity->value(tmp);
        } else {
          char tmp[20];
          snprintf(tmp, 20, "优化部件 %i", assmThread->currentActionParameter()+1);
          OutputActivity->value(tmp);
        }
        break;
      case solveThread_c::ACT_ASSEMBLING:
        OutputActivity->value("装配中");
        break;
      case solveThread_c::ACT_DISASSEMBLING:
        OutputActivity->value("拆解中");
        break;
      case solveThread_c::ACT_PAUSING:
        OutputActivity->value("已暂停");
        break;
      case solveThread_c::ACT_FINISHED:
        OutputActivity->value("已完成");
        break;
      case solveThread_c::ACT_WAIT_TO_STOP:
        OutputActivity->value("请等待");
        break;
      case solveThread_c::ACT_ERROR:
        OutputActivity->value("错误");
        break;
      }

      if (&(assmThread->getProblem()) == puzzle->getProblem(prob)) {

        // for the actually solved problem we enable the stop button
        BtnStart->deactivate();
        if (BtnPrepare) BtnPrepare->deactivate();
        BtnCont->deactivate();
        BtnStop->activate();

        // we can not edit solutions for a currently solved problem
        BtnSrtFind->deactivate();
        BtnSrtLevel->deactivate();
        BtnSrtMoves->deactivate();
        BtnSrtPieces->deactivate();
        BtnDelAll->deactivate();
        BtnDelBefore->deactivate();
        BtnDelAt->deactivate();
        BtnDelAfter->deactivate();
        BtnDelDisasm->deactivate();
        BtnDisasmDel->deactivate();
        BtnDisasmDelAll->deactivate();
        BtnDisasmAdd->deactivate();
        BtnDisasmAddAll->deactivate();
        BtnDisasmAddMissing->deactivate();

      } else {

        // all other problems can do nothing
        BtnStart->deactivate();
        if (BtnPrepare) BtnPrepare->deactivate();
        BtnCont->deactivate();
        BtnStop->deactivate();
        if (BtnPrepare) BtnPrepare->deactivate();

      }

    } else {

      pieceEdit->activate();

      // no thread currently calculating

      // so we can not stop the thread
      BtnStop->deactivate();

      // the stop button might be pressed when the thread finished, this might happen
      // relatively often, so we clear the state of that button
      BtnStop->clear();

      if (prob < puzzle->getNumberOfProblems()) {

        problem_c * pr = puzzle->getProblem(prob);
        // a valid problem is selected

        switch(pr->getSolveState()) {
        case SS_UNSOLVED:
          OutputActivity->value("空闲");
          BtnCont->deactivate();
          break;
        case SS_SOLVED:
          OutputActivity->value("已完成");
          BtnCont->deactivate();
          break;
        case SS_SOLVING:
          OutputActivity->value("已暂停");
          BtnCont->activate();
          break;
        case SS_UNKNOWN:
          OutputActivity->value("部分完成");
          BtnCont->deactivate();
          break;

        }

        // if we have a result and at least one piece, we can give it a try
        if ((pr->getNumberOfPieces() > 0) && (pr->resultValid())) {
          BtnStart->activate();
          if (BtnPrepare) BtnPrepare->activate();
        } else {
          BtnStart->deactivate();
          if (BtnPrepare) BtnPrepare->deactivate();
        }

      } else {

        // no start possible, when no valid problem selected
        BtnStart->deactivate();
        if (BtnPrepare) BtnPrepare->deactivate();
        BtnCont->deactivate();
        if (BtnPrepare) BtnPrepare->deactivate();
      }
    }
  }

  autoSave();

  TaskSelectionTab->redraw();
}

void mainWindow_c::update(void) {

  if (assmThread) {

    // check if the thread has thrown an exception. if so, re-throw it
    if (assmThread->currentAction() == solveThread_c::ACT_ASSERT) {

      assertWindow_c * aw = new assertWindow_c("由于内部错误，当前拼图\n"
                                               "无法求解\n",
                                               &(assmThread->getAssertException()));

      aw->show();

      while (aw->visible())
        Fl::wait();

      delete aw;
      delete assmThread;
      assmThread = 0;
      updateInterface();
      return;
    }

    // check, if the thread has stopped, if so then delete the object
    if ((assmThread->currentAction() == solveThread_c::ACT_PAUSING) ||
        (assmThread->currentAction() == solveThread_c::ACT_FINISHED)) {

      delete assmThread;
      assmThread = 0;

    } else if (assmThread->currentAction() == solveThread_c::ACT_ERROR) {

      unsigned int selectShape = 0xFFFFFFFF;

      switch(assmThread->getErrorState()) {
      case assembler_c::ERR_TOO_MANY_UNITS:
        fl_message("部件超出 %i 个单位", assmThread->getErrorParam());
        break;
      case assembler_c::ERR_TOO_FEW_UNITS:
        fl_message("部件少于所需数量 %i 个单位\n"
                   "请参阅用户指南章节\n"
                   "1.3.2.1 '体素状态'\n"
                   "3.4.2 'Basic Drawing Tools' and\n"
                   "3.8 'Miscellaneous Editing Tools'", assmThread->getErrorParam());
        break;
      case assembler_c::ERR_CAN_NOT_PLACE:
        fl_message("部件 %i 无法放置在结果中的任何位置", assmThread->getErrorParam()+1);
        selectShape = assmThread->getErrorParam();
        break;
      case assembler_c::ERR_CAN_NOT_RESTORE_VERSION:
        fl_message("无法恢复保存的状态，因为内部格式已更改。\n"
                   "您必须从头开始，或使用旧版 BurrTools 完成，抱歉");
        break;
      case assembler_c::ERR_CAN_NOT_RESTORE_SYNTAX:
        fl_message("无法恢复保存的状态，因为数据存在问题。\n"
                   "您必须从头开始，抱歉");
        break;
      case assembler_c::ERR_PUZZLE_UNHANDABLE:
        fl_message("程序出现问题，无法求解您的拼图定义。\n"
                   "请将拼图文件发送给程序员！");
        break;
      default:
        break;
      }

      if (selectShape < puzzle->getNumberOfShapes()) {
        TaskSelectionTab->value(TabPieces);
        PcSel->setSelection(selectShape);
        activateShape(PcSel->getSelection());
        updateInterface();
        StatPieceInfo(PcSel->getSelection());
      }

      delete assmThread;
      assmThread = 0;
    }

    // update the window, either when the thread stopped and so the buttons need to
    // be updated, or then the thread works for the currently selected problem
    if (!assmThread || &(assmThread->getProblem()) == puzzle->getProblem(solutionProblem->getSelection()))
      updateInterface();
  }
}

void mainWindow_c::Toggle3DView(void)
{
  // select the pieces tab, as exchanging widgets while they are invisible
  // didn't work. Save the current tab before that
  // this is required when changing the tab and we need to get the 3D view back
  // in the large window
  TaskSelectionTab->when(0);
  Fl_Widget *v = TaskSelectionTab->value();
  if (v != TabPieces) TaskSelectionTab->value(TabPieces);

  // exchange widget positions
  Fl_Group * tmp = pieceEdit->parent();
  View3D->parent()->add(pieceEdit);
  tmp->add(View3D);

  // exchange sizes
  {
    int x = pieceEdit->x();
    int y = pieceEdit->y();
    int w = pieceEdit->w();
    int h = pieceEdit->h();
    pieceEdit->resize(View3D->x(), View3D->y(), View3D->w(), View3D->h());
    View3D->resize(x, y, w, h);
  }

  // exchange grid positions
  {
    unsigned int x1, y1, w1, h1, x2, y2, w2, h2;
    pieceEdit->getGridValues(&x1, &y1, &w1, &h1);
    View3D->getGridValues   (&x2, &y2, &w2, &h2);

    pieceEdit->setGridValues( x2,  y2,  w2,  h2);
    View3D->setGridValues   ( x1,  y1,  w1,  h1);
  }

  is3DViewBig = !is3DViewBig;

  if (is3DViewBig)
    pieceEdit->parent()->resizable(pieceEdit);
  else
    View3D->parent()->resizable(View3D);

  // restore the old selected tab
  if (v != TabPieces) TaskSelectionTab->value(v);
  TaskSelectionTab->when(FL_WHEN_CHANGED);
}

void mainWindow_c::Big3DView(void) {
  if (!is3DViewBig) Toggle3DView();
  View3D->show();
  redraw();
}

void mainWindow_c::Small3DView(void) {
  if (is3DViewBig) Toggle3DView();
  pieceEdit->show();
  redraw();
}

int mainWindow_c::handle(int event) {

  if (Fl_Double_Window::handle(event))
    return 1;

  switch(event) {
  case FL_SHORTCUT:
    if (Fl::event_length()) {
      switch (Fl::event_text()[0]) {
      case '+':
        if (TaskSelectionTab->value() == TabPieces) {
          pieceEdit->setZ(pieceEdit->getZ()+1);
          return 1;
        }
        break;
      case '-':
        if (TaskSelectionTab->value() == TabPieces) {
          if (pieceEdit->getZ() > 0)
            pieceEdit->setZ(pieceEdit->getZ()-1);
          return 1;
        }
        break;
      }
    }
    switch(Fl::event_key()) {
      case FL_F + 5:
        if (TaskSelectionTab->value() == TabPieces) {
          editChoice->select(0);
          return 1;
        }
        break;
      case FL_F + 6:
        if (TaskSelectionTab->value() == TabPieces) {
          editChoice->select(1);
          return 1;
        }
        break;
      case FL_F + 7:
        if (TaskSelectionTab->value() == TabPieces) {
          editChoice->select(2);
          return 1;
        }
        break;
      case FL_F + 8:
        if (TaskSelectionTab->value() == TabPieces) {
          editChoice->select(3);
          return 1;
        }
        break;
    }
  }

  return 0;
}

#define SZ_GAP 5                               // gap between elements

void mainWindow_c::CreateShapeTab(void) {

  TabPieces = new layouter_c();
  TabPieces->label("实体");
  TabPieces->tooltip("编辑形状");
  TabPieces->clear_visible_focus();

  LFl_Tile * tile = new LFl_Tile(0, 0, 1, 1);
  tile->pitch(SZ_GAP);

  {
    layouter_c * group = new layouter_c(0, 0);
    group->box(FL_FLAT_BOX);

    new LSeparator_c(0, 0, 1, 1, "形状", false);

    layouter_c * o = new layouter_c(0, 1);

    BtnNewShape =   new LFlatButton_c(0, 0, 1, 1, "新建", " 添加新部件 ", cb_NewShape_stub, this);
    ((LFlatButton_c*)BtnNewShape)->weight(1, 0);
    (new LFl_Box(1, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDelShape =   new LFlatButton_c(2, 0, 1, 1, "删除", " 删除选中的部件 ", cb_DeleteShape_stub, this);
    ((LFlatButton_c*)BtnDelShape)->weight(1, 0);
    (new LFl_Box(3, 0))->setMinimumSize(SZ_GAP, 0);
    BtnCpyShape =   new LFlatButton_c(4, 0, 1, 1, "复制", " 复制选中的部件 ", cb_CopyShape_stub, this);
    ((LFlatButton_c*)BtnCpyShape)->weight(1, 0);
    (new LFl_Box(5, 0))->setMinimumSize(SZ_GAP, 0);
    BtnRenShape =   new LFlatButton_c(6, 0, 1, 1, "重命名", " 给选中的形状命名 ", cb_NameShape_stub, this);
    ((LFlatButton_c*)BtnRenShape)->weight(1, 0);
    (new LFl_Box(7, 0))->setMinimumSize(SZ_GAP, 0);
    BtnWeightInc =  new LFlatButton_c(8, 0, 1, 1, "权重+", " 增加选中形状的权重 ",cb_WeightInc_stub, this);
    (new LFl_Box(9, 0))->setMinimumSize(SZ_GAP, 0);
    BtnWeightDec =  new LFlatButton_c(10, 0, 1, 1, "权重-", " 减少选中形状的权重 ",cb_WeightDec_stub, this);
    (new LFl_Box(11, 0))->setMinimumSize(SZ_GAP, 0);
    BtnShapeLeft =  new LFlatButton_c(12, 0, 1, 1, "@-14->", " Exchange current shape with previous shape ", cb_ShapeLeft_stub, this);
    (new LFl_Box(13, 0))->setMinimumSize(SZ_GAP, 0);
    BtnShapeRight = new LFlatButton_c(14, 0, 1, 1, "@-16->", " Exchange current shape with next shape ", cb_ShapeRight_stub, this);
    (new LFl_Box(15, 0))->setMinimumSize(SZ_GAP, 0);
    o->end();

    (new LFl_Box(0, 2))->setMinimumSize(0, SZ_GAP);

    PcSel = new PieceSelector(0, 0, 200, 200, puzzle);
    LBlockListGroup_c * selGroup = new LBlockListGroup_c(0, 3, 1, 1, PcSel);
    selGroup->callback(cb_PcSel_stub, this);
    selGroup->tooltip(" 选择要编辑的形状 ");
    selGroup->weight(1, 1);

    group->end();
  }

  {
    layouter_c * group = new layouter_c(0, 1);
    group->box(FL_FLAT_BOX);

    new LSeparator_c(0, 0, 1, 1, "编辑", true);

    pieceTools = new ToolTabContainer(0, 1, 1, 1, ggt, this);
    pieceTools->callback(cb_TransformPiece_stub, this);

    (new LFl_Box(0, 2, 1, 1))->setMinimumSize(0, 5);

    layouter_c * o2 = new layouter_c(0, 3, 1, 1);

    editChoice = new ButtonGroup_c(0, 0, 1, 1);

    Fl_Button * b;
    b = editChoice->addButton();
    b->image(pm.get(TB_Color_Pen_Fixed_xpm));
    b->tooltip(" 添加普通体素到形状 F5 ");

    b = editChoice->addButton();
    b->image(pm.get(TB_Color_Pen_Variable_xpm));
    b->tooltip(" 添加可变体素到形状 F6 ");

    b = editChoice->addButton();
    b->image(pm.get(TB_Color_Eraser_xpm));
    b->tooltip(" 从形状中移除体素 F7 ");

    b = editChoice->addButton();
    b->image(pm.get(TB_Color_Brush_xpm));
    b->tooltip(" 更改形状中体素的约束颜色 F8 ");

    editChoice->callback(cb_EditChoice_stub, this);

    (new LFl_Box(1, 0, 1, 1))->setMinimumSize(5, 0);

    editMode = new ButtonGroup_c(2, 0, 1, 1);

    b = editMode->addButton();
    b->image(pm.get(TB_Color_Mouse_Rubber_Band_xpm));
    b->tooltip(" 在网格编辑器中拖拽矩形区域进行修改 ");

    b = editMode->addButton();
    b->image(pm.get(TB_Color_Mouse_Drag_xpm));
    b->tooltip(" 在网格编辑器中涂绘进行修改 ");

    editMode->callback(cb_EditMode_stub, this);

    (new LFl_Box(3, 0, 1, 1))->setMinimumSize(5, 0);

    LToggleButton_c * btn;

    btn = new LToggleButton_c(4, 0, 1, 1, cb_EditSym_stub, this, gridEditor_c::TOOL_MIRROR_X);
    btn->image(pm.get(TB_Color_Symmetrical_X_xpm));
    btn->tooltip(" 切换沿 Y-Z 平面的镜像 ");

    btn = new LToggleButton_c(5, 0, 1, 1, cb_EditSym_stub, this, gridEditor_c::TOOL_MIRROR_Y);
    btn->image(pm.get(TB_Color_Symmetrical_Y_xpm));
    btn->tooltip(" 切换沿 X-Z 平面的镜像 ");

    btn = new LToggleButton_c(6, 0, 1, 1, cb_EditSym_stub, this, gridEditor_c::TOOL_MIRROR_Z);
    btn->image(pm.get(TB_Color_Symmetrical_Z_xpm));
    btn->tooltip(" 切换沿 X-Y 平面的镜像 ");

    (new LFl_Box(7, 0, 1, 1))->setMinimumSize(5, 0);

    btn = new LToggleButton_c(8, 0, 1, 1, cb_EditSym_stub, this, gridEditor_c::TOOL_STACK_X);
    btn->image(pm.get(TB_Color_Columns_X_xpm));
    btn->tooltip(" 切换在所有 X 层绘制 ");

    btn = new LToggleButton_c(9, 0, 1, 1, cb_EditSym_stub, this, gridEditor_c::TOOL_STACK_Y);
    btn->image(pm.get(TB_Color_Columns_Y_xpm));
    btn->tooltip(" 切换在所有 Y 层绘制 ");

    btn = new LToggleButton_c(10, 0, 1, 1, cb_EditSym_stub, this, gridEditor_c::TOOL_STACK_Z);
    btn->image(pm.get(TB_Color_Columns_Z_xpm));
    btn->tooltip(" 切换在所有 Z 层绘制 ");

    (new LFl_Box(11, 0, 1, 1))->weight(1, 0);

    o2->end();

    (new LFl_Box(0, 4, 1, 1))->setMinimumSize(0, 5);

    pieceEdit = new VoxelEditGroup_c(0, 5, 1, 1, puzzle, ggt);
    pieceEdit->callback(cb_pieceEdit_stub, this);
    pieceEdit->end();
    pieceEdit->editType(gridEditor_c::EDT_RUBBER);
    pieceEdit->weight(0, 1);

    group->end();
  }

  {
    layouter_c * group = new layouter_c(0, 2);
    group->box(FL_FLAT_BOX);

    new LSeparator_c(0, 0, 1, 1, "颜色", true);

    layouter_c * o = new layouter_c(0, 1);

    BtnNewColor = new LFlatButton_c(0, 0, 1, 1, "添加", " 添加新颜色 ", cb_AddColor_stub, this);
    ((LFlatButton_c*)BtnNewColor)->weight(1, 0);
    (new LFl_Box(1, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDelColor = new LFlatButton_c(2, 0, 1, 1, "移除", " 移除选中的颜色 ", cb_RemoveColor_stub, this);
    ((LFlatButton_c*)BtnDelColor)->weight(1, 0);
    (new LFl_Box(3, 0))->setMinimumSize(SZ_GAP, 0);
    BtnChnColor = new LFlatButton_c(4, 0, 1, 1, "编辑", " 修改选中的颜色 ", cb_ChangeColor_stub, this);
    ((LFlatButton_c*)BtnChnColor)->weight(1, 0);

    o->end();

    (new LFl_Box(0, 2))->setMinimumSize(0, SZ_GAP);

    colorSelector = new ColorSelector(0, 0, 200, 200, puzzle, true);
    LBlockListGroup_c * colGroup = new LBlockListGroup_c(0, 3, 1, 1, colorSelector);
    colGroup->callback(cb_ColSel_stub, this);
    colGroup->tooltip(" 选择用于所有编辑操作的颜色 ");
    colGroup->weight(1, 1);

    group->end();
  }

  tile->end();

  TabPieces->resizable(tile);
  TabPieces->end();

  Fl_Group::current()->resizable(TabPieces);
}

void mainWindow_c::CreateProblemTab(void) {

  TabProblems = new layouter_c();
  TabProblems->label("拼图");
  TabProblems->tooltip("编辑问题");
  TabProblems->hide();
  TabProblems->clear_visible_focus();

  LFl_Tile * tile = new LFl_Tile(0, 0, 1, 1);
  tile->pitch(SZ_GAP);

  {
    layouter_c * group = new layouter_c(0, 0);
    group->box(FL_FLAT_BOX);

    new LSeparator_c(0, 0, 1, 1, "问题", false);

    layouter_c * o = new layouter_c(0, 1);

    BtnNewProb = new LFlatButton_c(0, 0, 1, 1, "新建", " 添加新问题 ", cb_NewProblem_stub, this);
    ((LFlatButton_c*)BtnNewProb)->weight(1, 0);
    (new LFl_Box(1, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDelProb = new LFlatButton_c(2, 0, 1, 1, "删除", " 删除选中的问题 ", cb_DeleteProblem_stub, this);
    ((LFlatButton_c*)BtnDelProb)->weight(1, 0);
    (new LFl_Box(3, 0))->setMinimumSize(SZ_GAP, 0);
    BtnCpyProb = new LFlatButton_c(4, 0, 1, 1, "复制", " 复制选中的问题 ", cb_CopyProblem_stub, this);
    ((LFlatButton_c*)BtnCpyProb)->weight(1, 0);
    (new LFl_Box(5, 0))->setMinimumSize(SZ_GAP, 0);
    BtnRenProb = new LFlatButton_c(6, 0, 1, 1, "重命名", " 重命名选中的问题 ", cb_RenameProblem_stub, this);
    ((LFlatButton_c*)BtnRenProb)->weight(1, 0);
    (new LFl_Box(7, 0))->setMinimumSize(SZ_GAP, 0);

    BtnProbLeft = new LFlatButton_c(8, 0, 1, 1, "@-14->", " Exchange current problem with previous problem ", cb_ProblemLeft_stub, this);
    (new LFl_Box(9, 0))->setMinimumSize(SZ_GAP, 0);
    BtnProbRight = new LFlatButton_c(10, 0, 1, 1, "@-16->", " Exchange current problem with next problem ", cb_ProblemRight_stub, this);

    o->end();

    (new LFl_Box(0, 2))->setMinimumSize(0, SZ_GAP);

    problemSelector = new ProblemSelector(0, 0, 100, 100, puzzle);
    LBlockListGroup_c * probGroup = new LBlockListGroup_c(0, 3, 1, 1, problemSelector);
    probGroup->callback(cb_ProbSel_stub, this);
    probGroup->tooltip(" 选择要编辑的问题 ");
    probGroup->weight(1, 1);

    group->end();
  }

  {
    layouter_c * group = new layouter_c(0, 1);
    group->box(FL_FLAT_BOX);

    new LSeparator_c(0, 0, 1, 1, "部件分配", true);

    layouter_c * o = new layouter_c(0, 1);

    problemResult = new ResultViewer_c(0, 0, 1, 1);
    problemResult->tooltip(" 当前问题的结果形状 ");
    problemResult->weight(1, 0);

    (new LFl_Box(1, 0))->setMinimumSize(SZ_GAP, 0);

    BtnSetResult = new LFlatButton_c(2, 0, 1, 1, "设为结果", " 将选中的形状设为结果 ", cb_ShapeToResult_stub, this);

    o->end();

    (new LFl_Box(0, 2))->setMinimumSize(0, SZ_GAP);

    shapeAssignmentSelector = new PieceSelector(0, 0, 100, 100, puzzle);
    LBlockListGroup_c * shapeGroup = new LBlockListGroup_c(0, 3, 1, 1, shapeAssignmentSelector);
    shapeGroup->callback(cb_ShapeSel_stub, this);
    shapeGroup->tooltip(" 选择要设为结果、添加或移除的形状 ");
    shapeGroup->weight(1, 1);

    group->end();
  }

  {
    layouter_c * group = new layouter_c(0, 2);
    group->box(FL_FLAT_BOX);

    new LSeparator_c(0, 0, 1, 1, 0, true);

    layouter_c * o = new layouter_c(0, 1);

    int xp = 0;

    BtnAddShape = new LFlatButton_c(xp++, 0, 1, 1, "+1", " Add another one of the selected shape ", cb_AddShapeToProblem_stub, this);
    ((LFlatButton_c*)BtnAddShape)->weight(1, 0);
    (new LFl_Box(xp++, 0))->setMinimumSize(SZ_GAP, 0);
    BtnRemShape = new LFlatButton_c(xp++, 0, 1, 1, "-1", " Remove one of the selected shapes ", cb_RemoveShapeFromProblem_stub, this);
    ((LFlatButton_c*)BtnRemShape)->weight(1, 0);
    (new LFl_Box(xp++, 0))->setMinimumSize(SZ_GAP, 0);
    BtnMinZero = new LFlatButton_c(xp++, 0, 1, 1, "最小=0", " 将部件最小数量设为0 ", cb_SetShapeMinimumToZero_stub, this);
    ((LFlatButton_c*)BtnMinZero)->weight(1, 0);
    (new LFl_Box(xp++, 0))->setMinimumSize(SZ_GAP, 0);
    BtnAddAll = new LFlatButton_c(xp++, 0, 1, 1, "全部+1", " 除结果外所有形状各加一个 ", cb_AddAllShapesToProblem_stub, this);
    ((LFlatButton_c*)BtnAddAll)->weight(1, 0);
    (new LFl_Box(xp++, 0))->setMinimumSize(SZ_GAP, 0);
    BtnRemAll = new LFlatButton_c(xp++, 0, 1, 1, "清除", " 移除所有部件 ", cb_RemoveAllShapesFromProblem_stub, this);
    ((LFlatButton_c*)BtnRemAll)->weight(1, 0);
    (new LFl_Box(xp++, 0))->setMinimumSize(SZ_GAP, 0);
    BtnGroup =    new LFlatButton_c(xp++, 0, 1, 1, "详情", " 编辑问题详情 ", cb_ShapeGroup_stub, this);
    ((LFlatButton_c*)BtnGroup)->weight(1, 0);
    (new LFl_Box(xp++, 0))->setMinimumSize(SZ_GAP, 0);
    BtnProbShapeLeft = new LFlatButton_c(xp++, 0, 1, 1, "@-14->", " Exchange current shape with previous shape ", cb_ProbShapeLeft_stub, this);
    (new LFl_Box(xp++, 0))->setMinimumSize(SZ_GAP, 0);
    BtnProbShapeRight = new LFlatButton_c(xp++, 0, 1, 1, "@-16->", " Exchange current shape with next shape ", cb_ProbShapeRight_stub, this);

    o->end();

    (new LFl_Box(0, 2))->setMinimumSize(0, SZ_GAP);

    PiecesCountList = new PiecesList(0, 0, 100, 100);
    LBlockListGroup_c * shapeGroup = new LBlockListGroup_c(0, 3, 1, 1, PiecesCountList);
    shapeGroup->callback(cb_PiecesClicked_stub, this);
    shapeGroup->tooltip(" 显示当前问题中使用了哪些形状及使用次数，可用于选择形状 ");
    shapeGroup->weight(1, 1);

    group->end();
  }

  {
    layouter_c * group = new layouter_c(0, 3);
    group->box(FL_FLAT_BOX);

    new LSeparator_c(0, 0, 1, 1, "颜色分配", true);

    colorAssignmentSelector = new ColorSelector(0, 0, 100, 100, puzzle, false);
    LBlockListGroup_c * colGroup = new LBlockListGroup_c(0, 1, 1, 1, colorAssignmentSelector);
    colGroup->callback(cb_ColorAssSel_stub, this);
    colGroup->tooltip(" Select colour to add or remove from constraints ");
    colGroup->weight(1, 1);

    group->end();
  }

  {
    layouter_c * group = new layouter_c(0, 4);
    group->box(FL_FLAT_BOX);

    new LSeparator_c(0, 0, 1, 1, 0, true);

    layouter_c * o = new layouter_c(0, 1);

    BtnColSrtPc = new LFlatButton_c(0, 0, 1, 1, "按部件排序", " 按部件对颜色约束排序 ", cb_CCSortByPiece_stub, this);
    ((LFlatButton_c*)BtnColSrtPc)->weight(1, 0);
    (new LFl_Box(1, 0))->setMinimumSize(SZ_GAP, 0);
    BtnColAdd = new LFlatButton_c(2, 0, 1, 1, "@-12->", " Add colour to constraint ", cb_AllowColor_stub, this);
    BtnColRem = new LFlatButton_c(3, 0, 1, 1, "@-18->", " Add colour to constraint ", cb_DisallowColor_stub, this);
    (new LFl_Box(4, 0))->setMinimumSize(SZ_GAP, 0);
    BtnColSrtRes = new LFlatButton_c(5, 0, 1, 1, "按结果排序", " 按结果对颜色约束排序 ", cb_CCSortByResult_stub, this);
    ((LFlatButton_c*)BtnColSrtRes)->weight(1, 0);

    o->end();

    (new LFl_Box(0, 2))->setMinimumSize(0, SZ_GAP);

    colconstrList = new ColorConstraintsEdit(0, 0, 100, 100, puzzle);
    LConstraintsGroup_c * colGroup = new LConstraintsGroup_c(0, 3, 1, 1, colconstrList);
    colGroup->callback(cb_ColorConstrSel_stub, this);
    colGroup->tooltip(" Colour constraints for the current problem ");
    colGroup->weight(1, 1);

    group->end();
  }

  tile->end();

  TabProblems->resizable(tile);
  TabProblems->end();
}

void mainWindow_c::CreateSolveTab(void) {

  TabSolve = new layouter_c();
  TabSolve->label("求解器");
  TabSolve->tooltip("求解问题");
  TabSolve->hide();
  TabSolve->clear_visible_focus();

  LFl_Tile * tile = new LFl_Tile(0, 0, 1, 1);
  tile->pitch(SZ_GAP);

  {
    layouter_c * group = new layouter_c(0, 0);
    group->box(FL_FLAT_BOX);

    new LSeparator_c(0, 0, 1, 1, "参数", false);

    solutionProblem = new ProblemSelector(0, 0, 100, 100, puzzle);
    LBlockListGroup_c * shapeGroup = new LBlockListGroup_c(0, 1, 1, 1, solutionProblem);
    shapeGroup->callback(cb_SolProbSel_stub, this);
    shapeGroup->tooltip(" Select problem to solve ");
    shapeGroup->weight(1, 1);

    layouter_c * o = new layouter_c(0, 2);

    SolveDisasm = new LFl_Check_Button("拆解", 0, 0, 1, 1);
    SolveDisasm->tooltip(" Do also try to disassemble the assembled puzzles. Only puzzles that can be disassembled will be added to solutions ");
    SolveDisasm->clear_visible_focus();

    JustCount = new LFl_Check_Button("仅计数", 0, 1, 1, 1);
    JustCount->tooltip(" Don\'t save the solutions, just count the number of them ");
    JustCount->clear_visible_focus();

    CompleteRotations = new LFl_Check_Button("详细旋转检查", 0, 2, 1, 1);
    CompleteRotations->tooltip(" Do expensive and thorough rotation check, eliminating translations and rotations not in symmetry of the result shape ");
    CompleteRotations->clear_visible_focus();

    DropDisassemblies = new LFl_Check_Button("丢弃拆解", 1, 0, 1, 1);
    DropDisassemblies->tooltip(" Don\'t save the Disassemblies, just the information about them ");
    DropDisassemblies->clear_visible_focus();

    KeepMirrors = new LFl_Check_Button("保留镜像解", 1, 1, 1, 1);
    KeepMirrors->tooltip(" Don't remove solutions that are mirrors of another solution ");
    KeepMirrors->clear_visible_focus();

    KeepRotations = new LFl_Check_Button("保留旋转解", 1, 2, 1, 1);
    KeepRotations->tooltip(" Don't remove solutions that are rotations of other solutions ");
    KeepRotations->clear_visible_focus();

    o->end();

    o = new layouter_c(0, 3);

    new LFl_Box("排序方式：", 0, 0, 1, 1);

    sortMethod = new LFl_Choice(1, 0, 1, 1);
    ((LFl_Choice*)sortMethod)->weight(1, 0);

    // be careful the order in here must correspond with the enumeration in assembler thread
    sortMethod->add("未排序");
    sortMethod->add("完全拆解步数");
    sortMethod->add("级别");

    sortMethod->value(1);

    o->end();

    (new LFl_Box(0, 4))->setMinimumSize(0, SZ_GAP);

    o = new layouter_c(0, 5);

    new LFl_Box("丢弃 ", 0, 0, 1, 1);
    new LFl_Box("限制 ", 3, 0, 1, 1);
    (new LFl_Box(2, 0))->setMinimumSize(SZ_GAP, 0);

    solDrop = new LFl_Value_Input(1, 0, 1, 1);
    solLimit = new LFl_Value_Input(4, 0, 1, 1);
    solDrop->bounds(1, 100000000);
    solLimit->bounds(1, 100000000);
    solLimit->step(1, 1);
    solDrop->step(1, 1);

    solDrop->value(1);
    solLimit->value(100);

    ((LFl_Value_Input*)solDrop)->weight(1, 0);
    ((LFl_Value_Input*)solLimit)->weight(1, 0);

    o->end();

    (new LFl_Box(0, 6))->setMinimumSize(0, SZ_GAP);

    o = new layouter_c(0, 7);

    if (expertMode) {
      BtnPrepare = new LFlatButton_c(0, 0, 1, 1, "准备", " 执行准备阶段然后停止，这会移除旧结果 ", cb_BtnPrepare_stub, this);
      ((LFlatButton_c*)BtnPrepare)->weight(1, 0);
      (new LFl_Box(1, 0))->setMinimumSize(SZ_GAP, 0);
    } else
      BtnPrepare = 0;
    BtnStart = new LFlatButton_c(2, 0, 1, 1, "开始", " 开始新的求解过程，移除旧结果 ", cb_BtnStart_stub, this);
    ((LFlatButton_c*)BtnStart)->weight(1, 0);
    (new LFl_Box(3, 0))->setMinimumSize(SZ_GAP, 0);
    BtnCont = new LFlatButton_c(4, 0, 1, 1, "继续", " 继续已开始的过程 ", cb_BtnCont_stub, this);
    ((LFlatButton_c*)BtnCont)->weight(1, 0);
    (new LFl_Box(5, 0))->setMinimumSize(SZ_GAP, 0);
    BtnStop = new LFlatButton_c(6, 0, 1, 1, "停止", " 停止当前正在运行的求解过程 ", cb_BtnStop_stub, this);
    ((LFlatButton_c*)BtnStop)->weight(1, 0);

    o->end();

    (new LFl_Box(0, 8))->setMinimumSize(0, SZ_GAP);

    o = new layouter_c(0, 9);

    BtnPlacement = new LFlatButton_c(0, 0, 1, 1, "放置", " 浏览计算出的部件放置 ", cb_BtnPlacementBrowser_stub, this);
    ((LFlatButton_c*)BtnPlacement)->weight(1, 0);

    (new LFl_Box(1, 0))->setMinimumSize(SZ_GAP, 0);

    BtnMovement = new LFlatButton_c(2, 0, 1, 1, "运动", " 浏览装配体的可能运动 ", cb_BtnMovementBrowser_stub, this);
    ((LFlatButton_c*)BtnMovement)->weight(1, 0);

    if (expertMode)
    {
      (new LFl_Box(3, 0))->setMinimumSize(SZ_GAP, 0);

      BtnStep = new LFlatButton_c(4, 0, 1, 1, "单步", " 在装配器中执行一步 ", cb_BtnAssemblerStep_stub, this);
      ((LFlatButton_c*)BtnStep)->weight(1, 0);
    } else
      BtnStep = 0;

    o->end();

    (new LFl_Box(0, 10))->setMinimumSize(0, SZ_GAP);

    SolvingProgress = new LFl_Progress(0, 11, 1, 1);
    SolvingProgress->tooltip(" Percentage of solution space searched ");
    SolvingProgress->box(FL_ENGRAVED_BOX);
    SolvingProgress->selection_color((Fl_Color)4);
    SolvingProgress->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

    o = new layouter_c(0, 12);

    (new LFl_Box("活动：", 0, 0, 1, 1))->stretchRight();
    OutputActivity = new LFl_Output(1, 0, 3, 1);
    OutputActivity->box(FL_FLAT_BOX);
    OutputActivity->color(FL_BACKGROUND_COLOR);
    OutputActivity->tooltip(" What is currently done ");
    OutputActivity->clear_visible_focus();

    (new LFl_Box("装配数：", 0, 1, 1, 1))->stretchRight();
    OutputAssemblies = new LFl_Value_Output(1, 1, 1, 1);
    OutputAssemblies->box(FL_FLAT_BOX);
    OutputAssemblies->step(1);   // make output NOT use scientific presentation for big numbers
    OutputAssemblies->tooltip(" Number of assemblies found so far ");
    ((LFl_Value_Output*)OutputAssemblies)->weight(2, 0);

    (new LFl_Box("解数：", 0, 2, 1, 1))->stretchRight();
    OutputSolutions = new LFl_Value_Output(1, 2, 1, 1);
    OutputSolutions->box(FL_FLAT_BOX);
    OutputSolutions->step(1);    // make output NOT use scientific presentation for big numbers
    OutputSolutions->tooltip(" Number of solutions (assemblies that can be disassembled) found so far ");

    (new LFl_Box("已用时间：", 2, 1, 1, 1))->stretchRight();
    TimeUsed = new LFl_Output(3, 1, 1, 1);
    TimeUsed->box(FL_NO_BOX);
    ((LFl_Output*)TimeUsed)->weight(4, 0);

    (new LFl_Box("剩余时间：", 2, 2, 1, 1))->stretchRight();
    TimeEst = new LFl_Output(3, 2, 1, 1);
    TimeEst->box(FL_NO_BOX);
    TimeEst->tooltip(" This is a very approximate estimate and can be totally wrong, to take with a grain of salt ");

    o->end();

    group->end();
  }

  {
    layouter_c * group = new layouter_c(0, 1);
    group->box(FL_FLAT_BOX);

    new LSeparator_c(0, 0, 1, 1, "解", true);

    layouter_c * o = new layouter_c(0, 1);

    new LFl_Box("解编号", 0, 0, 1, 1);

    // Gap between Solution and value
    (new LFl_Box(1, 0))->setMinimumSize(SZ_GAP, 0);

    SolutionsInfo = new LFl_Value_Output(2, 0, 1, 1);
    SolutionsInfo->tooltip(" Number of solutions ");
    SolutionsInfo->box(FL_FLAT_BOX);
    ((LFl_Value_Output*)SolutionsInfo)->weight(1, 0);

    SolutionSel = new LFl_Value_Slider(0, 1, 3, 1);
    SolutionSel->tooltip(" Select one Solution ");
    SolutionSel->value(1);
    SolutionSel->type(1);
    SolutionSel->step(1);
    SolutionSel->callback(cb_SolutionSel_stub, this);
    SolutionSel->align(FL_ALIGN_TOP_LEFT);

    (new LFl_Box(0, 3))->setMinimumSize(0, SZ_GAP);

    new LFl_Box("步数", 0, 4, 1, 1);

    MovesInfo = new LFl_Output(2, 4, 1, 1);
    MovesInfo->tooltip(" Steps for complete disassembly ");
    MovesInfo->box(FL_FLAT_BOX);
    MovesInfo->color(FL_BACKGROUND_COLOR);
    ((LFl_Output*)MovesInfo)->weight(1, 0);

    SolutionAnim = new LFl_Value_Slider(0, 5, 3, 1);
    SolutionAnim->tooltip(" Animate the disassembly ");
    SolutionAnim->type(1);
    SolutionAnim->step(0.02);
    SolutionAnim->callback(cb_SolutionAnim_stub, this);
    SolutionAnim->align(FL_ALIGN_TOP_LEFT);

    o->end();

    (new LFl_Box(0, 2))->setMinimumSize(0, SZ_GAP);

    o = new layouter_c(0, 3);

    new LFl_Box("装配体：", 0, 0, 1, 1);
    new LFl_Box("解：", 2, 0, 1, 1);

    AssemblyNumber = new LFl_Value_Output(1, 0, 1, 1);
    SolutionNumber = new LFl_Value_Output(3, 0, 1, 1);
    AssemblyNumber->box(FL_FLAT_BOX);
    SolutionNumber->box(FL_FLAT_BOX);
    AssemblyNumber->step(1);    // make output NOT use scientific presentation for big numbers
    SolutionNumber->step(1);    // make output NOT use scientific presentation for big numbers
    ((LFl_Value_Output*)AssemblyNumber)->weight(1, 0);
    ((LFl_Value_Output*)SolutionNumber)->weight(1, 0);

    o->end();

    (new LFl_Box(0, 4))->setMinimumSize(0, SZ_GAP);

    o = new layouter_c(0, 5);

    new LFl_Box("排序方式：", 0, 0);

    BtnSrtFind =  new LFlatButton_c(1, 0, 1, 1, "编号", " 按解被找到的顺序排序 ", cb_SrtFind_stub, this);
    ((LFlatButton_c*)BtnSrtFind)->weight(1, 0);
    (new LFl_Box(2, 0))->setMinimumSize(SZ_GAP, 0);
    BtnSrtLevel = new LFlatButton_c(3, 0, 1, 1, "级别", " 按级别递增排序 ", cb_SrtLevel_stub, this);
    ((LFlatButton_c*)BtnSrtLevel)->weight(1, 0);
    (new LFl_Box(4, 0))->setMinimumSize(SZ_GAP, 0);
    BtnSrtMoves = new LFlatButton_c(5, 0, 1, 1, "拆解步数", " 按完全拆解步数递增排序 ", cb_SrtMoves_stub, this);
    ((LFlatButton_c*)BtnSrtMoves)->weight(1, 0);
    (new LFl_Box(6, 0))->setMinimumSize(SZ_GAP, 0);
    BtnSrtPieces = new LFlatButton_c(7, 0, 1, 1, "部件数", " 按使用部件数排序 ", cb_SrtPieces_stub, this);
    ((LFlatButton_c*)BtnSrtPieces)->weight(1, 0);

    o->end();

    (new LFl_Box(0, 6))->setMinimumSize(0, SZ_GAP);

    o = new layouter_c(0, 7);

    new LFl_Box("删除：", 0, 0);

    BtnDelAll =    new LFlatButton_c(1, 0, 1, 1, "全部", " 删除所有解 ", cb_DelAll_stub, this);
    ((LFlatButton_c*)BtnDelAll)->weight(1, 0);
    (new LFl_Box(2, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDelBefore = new LFlatButton_c(3, 0, 1, 1, "之前", " 删除当前选中解之前的所有解 ", cb_DelBefore_stub, this);
    ((LFlatButton_c*)BtnDelBefore)->weight(1, 0);
    (new LFl_Box(4, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDelAt =     new LFlatButton_c(5, 0, 1, 1, "当前", " 删除当前解 ", cb_DelAt_stub, this);
    ((LFlatButton_c*)BtnDelAt)->weight(1, 0);
    (new LFl_Box(6, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDelAfter =  new LFlatButton_c(7, 0, 1, 1, "之后", " 删除当前选中解之后的所有解 ", cb_DelAfter_stub, this);
    ((LFlatButton_c*)BtnDelAfter)->weight(1, 0);
    (new LFl_Box(8, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDelDisasm = new LFlatButton_c(9, 0, 1, 1, "无拆解", " 删除所有没有有效拆解的解 ", cb_DelDisasmless_stub, this);
    ((LFlatButton_c*)BtnDelDisasm)->weight(1, 0);

    o->end();

    (new LFl_Box(0, 8))->setMinimumSize(0, SZ_GAP);

    o = new layouter_c(0, 9);

    BtnDisasmDel    = new LFlatButton_c(0, 0, 1, 1, "删拆解", " 移除当前解的拆解 ", cb_DelDisasm_stub, this);
    ((LFlatButton_c*)BtnDisasmDel)->weight(1, 0);
    (new LFl_Box(1, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDisasmDelAll = new LFlatButton_c(2, 0, 1, 1, "全删拆解", " 移除所有解的拆解 ", cb_DelAllDisasm_stub, this);
    ((LFlatButton_c*)BtnDisasmDelAll)->weight(1, 0);
    (new LFl_Box(3, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDisasmAdd    = new LFlatButton_c(4, 0, 1, 1, "加拆解", " 重新计算当前解的拆解 ", cb_AddDisasm_stub, this);
    ((LFlatButton_c*)BtnDisasmAdd)->weight(1, 0);
    (new LFl_Box(5, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDisasmAddAll = new LFlatButton_c(6, 0, 1, 1, "全加拆解", " 重新计算所有解的拆解 ", cb_AddAllDisasm_stub, this);
    ((LFlatButton_c*)BtnDisasmAddAll)->weight(1, 0);
    (new LFl_Box(7, 0))->setMinimumSize(SZ_GAP, 0);
    BtnDisasmAddMissing=new LFlatButton_c(8, 0, 1, 1, "补拆解", " 为没有有效拆解的解补充计算拆解 ", cb_AddMissingDisasm_stub, this);
    ((LFlatButton_c*)BtnDisasmAddMissing)->weight(1, 0);

    o->end();

    (new LFl_Box(0, 10))->setMinimumSize(0, SZ_GAP);

    PcVis = new PieceVisibility(0, 0, 100, 100);
    LBlockListGroup_c * shapeGroup = new LBlockListGroup_c(0, 11, 1, 1, PcVis);
    shapeGroup->callback(cb_PcVis_stub, this);
    shapeGroup->tooltip(" Change appearance of the pieces between normal, grid and invisible ");
    shapeGroup->weight(1, 1);

    group->end();
  }
  tile->end();

  TabSolve->resizable(tile);
  TabSolve->end();
}

void mainWindow_c::activateConfigOptions(void) {

  if (config.useTooltips())
    Fl_Tooltip::enable();
  else
    Fl_Tooltip::disable();

  View3D->getView()->useLightning(config.useLightning());
  View3D->getView()->setRotaterMethod(config.rotationMethod());
}

mainWindow_c::mainWindow_c(gridType_c * gt) : LFl_Double_Window(true) {

  assmThread = 0;
  fname = 0;
  disassemble = 0;
  editSymmetries = 0;
  expertMode = true;

  puzzle = new puzzle_c(gt);
  ggt = new guiGridType_c(puzzle->getGridType());
  changed = false;

  label("鲁班锁 - 未知");
  user_data((void*)(this));

  MainMenu = new LFl_Menu_Bar(0, 0, 1, 1);
  MainMenu->copy(menu_MainMenu, this);

  StatusLine = new LStatusLine(0, 2, 1, 1);
  StatusLine->callback(cb_Status_stub, this);

  LFl_Tile * mainTile = new LFl_Tile(0, 1, 1, 1);
  mainTile->weight(0, 1);

  layouter_c * lay = new layouter_c(1, 0, 1, 1);
  View3D = new LView3dGroup(0, 0, 1, 1);
  lay->weight(1, 0);
  lay->end();
  lay->setMinimumSize(400, 400);
  View3D->weight(0, 1);
  View3D->callback(cb_3dClick_stub, this);

  // this box paints the background behind the tab, because the tabs are partly transparent
  (new LFl_Box(0, 0, 1, 1))->color(FL_BACKGROUND_COLOR);

  // the tab for the tool bar
  TaskSelectionTab = new LFl_Tabs(0, 0, 1, 1);
  TaskSelectionTab->callback(cb_TaskSelectionTab_stub, this);
  TaskSelectionTab->clear_visible_focus();

  // the three tabs
  CreateShapeTab();
  CreateProblemTab();
  CreateSolveTab();

  currentTab = 0;
  ViewSizes[0] = -1;
  ViewSizes[1] = -1;
  ViewSizes[2] = -1;

  resize(config.windowPosX(), config.windowPosY(), config.windowPosW(), config.windowPosH());

  if (!config.useRubberband())
    editMode->select(1);
  else
    editMode->select(0);

  is3DViewBig = true;
  shapeEditorWithBig3DView = true;

  updateInterface();
  activateClear();

  // set the edit mode from the selected mode
  cb_EditChoice();

  activateConfigOptions();
}

mainWindow_c::~mainWindow_c() {

  config.windowPos(x(), y(), w(), h());

  if (assmThread) {
    delete assmThread;
    assmThread = 0;
  }

  delete puzzle;

  if (fname) {
    delete [] fname;
    fname = 0;
  }

  if (disassemble) {
    delete disassemble;
    disassemble = 0;
  }

  if (ggt)
    delete ggt;
}
