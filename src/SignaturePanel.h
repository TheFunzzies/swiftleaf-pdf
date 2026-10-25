/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#ifndef SIGNATURE_PANEL_H
#define SIGNATURE_PANEL_H

#include "shared/EngineAll.h"

struct MainWindow;

void ShowSignaturePanel(MainWindow* win);
void ShowCertifyDocumentDialog(MainWindow* win);
bool ClearDocumentSignature(MainWindow* win, Str fieldName = {});
void ViewSignatureCertificate(HWND hwndParent, const Str& certDer);
bool Signature_UnitTests();

#endif
