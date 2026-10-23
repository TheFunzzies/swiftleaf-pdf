/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

struct MainWindow;
struct Annotation;

bool IsFormAuthoringCommand(int cmdId);
Annotation* PlaceFormField(MainWindow*, int cmdId, int pageNo, RectF);
void EditFormFieldProperties(MainWindow*, Annotation*);
