/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Swiftleaf's document data panel (PDF layers and embedded files) and the
// banner that points out interactive form fields when a document has them.

struct MainWindow;

enum class DocumentPanelMode {
    Layers = 0,
    Attachments = 1,
};

void CreateDocumentPanel(MainWindow*);
void DestroyDocumentPanel(MainWindow*);
void DocumentPanelUpdate(MainWindow*);
void DocumentPanelAfterThemeChange(MainWindow*);
bool IsDocumentPanelVisible(MainWindow*, DocumentPanelMode);
void ToggleDocumentPanel(MainWindow*, DocumentPanelMode);
int DocumentPanelDx();

void CreateFormBanner(MainWindow*);
void DestroyFormBanner(MainWindow*);
void FormBannerUpdate(MainWindow*);
void FormBannerAfterThemeChange(MainWindow*);
