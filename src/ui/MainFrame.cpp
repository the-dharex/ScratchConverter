#include "ui/MainFrame.hpp"
#include "core/ConversionPipeline.hpp"

#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/statline.h>
#include <wx/stdpaths.h>

namespace sc {

wxDEFINE_EVENT(EVT_CONVERSION_UPDATE, wxThreadEvent);

// ─────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────
MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "ScratchConverter",
              wxDefaultPosition, wxSize(560, 520),
              wxDEFAULT_FRAME_STYLE & ~wxRESIZE_BORDER)
{
    SetMinSize(wxSize(560, 520));

    auto* panel = new wxPanel(this);
    auto* root  = new wxBoxSizer(wxVERTICAL);

    // ── Title banner ─────────────────────────────────────────
    auto* header = new wxStaticText(panel, wxID_ANY, "ScratchConverter");
    header->SetFont(header->GetFont().Bold().Scaled(1.6f));
    root->Add(header, 0, wxALL | wxALIGN_CENTER, 10);

    root->Add(new wxStaticLine(panel), 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    // ── File paths ───────────────────────────────────────────
    auto* pathGrid = new wxFlexGridSizer(2, 3, 6, 6);
    pathGrid->AddGrowableCol(1, 1);

    pathGrid->Add(new wxStaticText(panel, wxID_ANY, "Archivo .sb3:"),
                  0, wxALIGN_CENTER_VERTICAL);
    sb3PathCtrl_ = new wxTextCtrl(panel, wxID_ANY, "",
                                  wxDefaultPosition, wxSize(320, -1));
    sb3PathCtrl_->SetEditable(false);
    pathGrid->Add(sb3PathCtrl_, 1, wxEXPAND);
    auto* sb3Btn = new wxButton(panel, wxID_ANY, "Explorar...");
    pathGrid->Add(sb3Btn, 0);

    pathGrid->Add(new wxStaticText(panel, wxID_ANY, wxString::FromUTF8("Ruta exportación:")),
                  0, wxALIGN_CENTER_VERTICAL);
    exportPathCtrl_ = new wxTextCtrl(panel, wxID_ANY, "",
                                     wxDefaultPosition, wxSize(320, -1));
    exportPathCtrl_->SetEditable(false);
    pathGrid->Add(exportPathCtrl_, 1, wxEXPAND);
    auto* exportBtn = new wxButton(panel, wxID_ANY, "Explorar...");
    pathGrid->Add(exportBtn, 0);

    root->Add(pathGrid, 0, wxEXPAND | wxALL, 12);

    // ── Window settings ──────────────────────────────────────
    auto* winBox = new wxStaticBoxSizer(wxVERTICAL, panel,
                                        wxString::FromUTF8("Configuración de ventana"));
    auto* winGrid = new wxFlexGridSizer(2, 2, 6, 10);
    winGrid->AddGrowableCol(1, 1);

    winGrid->Add(new wxStaticText(panel, wxID_ANY, wxString::FromUTF8("Título:")),
                 0, wxALIGN_CENTER_VERTICAL);
    titleCtrl_ = new wxTextCtrl(panel, wxID_ANY, "Scratch Game");
    winGrid->Add(titleCtrl_, 1, wxEXPAND);

    winGrid->Add(new wxStaticText(panel, wxID_ANY, wxString::FromUTF8("Resolución:")),
                 0, wxALIGN_CENTER_VERTICAL);

    auto* resRow = new wxBoxSizer(wxHORIZONTAL);
    widthCtrl_  = new wxSpinCtrl(panel, wxID_ANY, "960", wxDefaultPosition,
                                 wxSize(80, -1), wxSP_ARROW_KEYS, 320, 3840, 960);
    heightCtrl_ = new wxSpinCtrl(panel, wxID_ANY, "720", wxDefaultPosition,
                                 wxSize(80, -1), wxSP_ARROW_KEYS, 240, 2160, 720);
    resRow->Add(widthCtrl_);
    resRow->Add(new wxStaticText(panel, wxID_ANY, "  x  "), 0, wxALIGN_CENTER_VERTICAL);
    resRow->Add(heightCtrl_);
    winGrid->Add(resRow);

    winBox->Add(winGrid, 0, wxEXPAND | wxALL, 6);

    fullscreenCtrl_ = new wxCheckBox(panel, wxID_ANY, "Pantalla completa");
    winBox->Add(fullscreenCtrl_, 0, wxALL, 6);

    root->Add(winBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    root->AddSpacer(8);

    // ── Target OS ────────────────────────────────────────────
    auto* osBox = new wxStaticBoxSizer(wxHORIZONTAL, panel, "Sistema objetivo");
    windowsRadio_ = new wxRadioButton(panel, wxID_ANY, "Windows",
                                      wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    linuxRadio_   = new wxRadioButton(panel, wxID_ANY, "Linux");
    windowsRadio_->SetValue(true);
    osBox->Add(windowsRadio_, 0, wxALL, 8);
    osBox->Add(linuxRadio_,   0, wxALL, 8);
    root->Add(osBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    root->AddSpacer(8);

    // ── Progress gauge ───────────────────────────────────────
    progressGauge_ = new wxGauge(panel, wxID_ANY, 100);
    progressGauge_->SetValue(0);
    root->Add(progressGauge_, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    root->AddSpacer(6);

    // ── Convert button ───────────────────────────────────────
    convertBtn_ = new wxButton(panel, wxID_ANY, "Convertir");
    convertBtn_->SetFont(convertBtn_->GetFont().Bold().Scaled(1.2f));
    root->Add(convertBtn_, 0, wxALIGN_CENTER | wxBOTTOM, 10);

    panel->SetSizerAndFit(root);

    // ── Status bar ───────────────────────────────────────────
    statusBar_ = CreateStatusBar();
    SetStatus("Listo");

    // ── Bind events ──────────────────────────────────────────
    sb3Btn->Bind(wxEVT_BUTTON, &MainFrame::OnBrowseSB3, this);
    exportBtn->Bind(wxEVT_BUTTON, &MainFrame::OnBrowseExport, this);
    convertBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnConvert, this);
    Bind(EVT_CONVERSION_UPDATE, &MainFrame::OnConversionUpdate, this);

    CenterOnScreen();
}

// ─────────────────────────────────────────────────────────────
// Event handlers
// ─────────────────────────────────────────────────────────────
void MainFrame::OnBrowseSB3(wxCommandEvent&) {
    wxFileDialog dlg(this, "Seleccionar archivo .sb3", "", "",
                     "Scratch 3 (*.sb3)|*.sb3",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK)
        sb3PathCtrl_->SetValue(dlg.GetPath());
}

void MainFrame::OnBrowseExport(wxCommandEvent&) {
    wxDirDialog dlg(this, wxString::FromUTF8("Seleccionar carpeta de exportación"),
                    "", wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK)
        exportPathCtrl_->SetValue(dlg.GetPath());
}

void MainFrame::OnConvert(wxCommandEvent&) {
    // Validate inputs
    if (sb3PathCtrl_->GetValue().IsEmpty()) {
        wxMessageBox("Selecciona un archivo .sb3", "Error",
                     wxICON_ERROR | wxOK, this);
        return;
    }
    if (exportPathCtrl_->GetValue().IsEmpty()) {
        wxMessageBox(wxString::FromUTF8("Selecciona una ruta de exportación"), "Error",
                     wxICON_ERROR | wxOK, this);
        return;
    }

    auto config = BuildConfig();
    EnableControls(false);
    progressGauge_->SetValue(0);

    // Resolve the runtime directory relative to the executable
    auto exePath = std::filesystem::path(
        wxStandardPaths::Get().GetExecutablePath().ToStdString());
    auto runtimeDir = exePath.parent_path() / "runtime";

    // Capture wxEvtHandler pointer for the worker
    wxEvtHandler* handler = this;

    worker_ = std::make_unique<std::jthread>(
        [config = std::move(config), runtimeDir, handler](std::stop_token stop) {
            auto postStatus = [handler](const std::string& msg, int progress = -1) {
                auto* evt = new wxThreadEvent(EVT_CONVERSION_UPDATE);
                evt->SetString(wxString::FromUTF8(msg));
                evt->SetInt(progress);
                wxQueueEvent(handler, evt);
            };

            ConversionPipeline pipeline(config, runtimeDir, std::move(postStatus));
            pipeline.Run(stop);
        });
}

void MainFrame::OnConversionUpdate(wxThreadEvent& evt) {
    wxString msg = evt.GetString();
    int progress = evt.GetInt();

    // Capture the executable path when reported
    if (msg.StartsWith("[EXEC] "))
        lastExePath_ = msg.Mid(7);

    SetStatus(msg);

    if (progress >= 0)
        progressGauge_->SetValue(progress);

    // Check for completion or error markers
    if (msg.StartsWith("[OK]") || msg.StartsWith("[ERROR]")) {
        EnableControls(true);
        if (worker_ && worker_->joinable()) {
            worker_->request_stop();
            worker_->join();
        }
        worker_.reset();

        if (msg.StartsWith("[OK]")) {
            progressGauge_->SetValue(100);
            if (!lastExePath_.IsEmpty()) {
                wxMessageBox(
                    wxString::FromUTF8("Ejecutable generado en:\n") + lastExePath_,
                    wxString::FromUTF8("Conversión completada"),
                    wxICON_INFORMATION | wxOK, this);
                lastExePath_.Clear();
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────
void MainFrame::SetStatus(const wxString& msg) {
    statusBar_->SetStatusText(msg);
}

ConversionConfig MainFrame::BuildConfig() const {
    ConversionConfig cfg;
    cfg.sb3Path    = sb3PathCtrl_->GetValue().ToStdString();
    cfg.exportPath = exportPathCtrl_->GetValue().ToStdString();
    cfg.windowTitle = titleCtrl_->GetValue().ToStdString();
    cfg.width       = widthCtrl_->GetValue();
    cfg.height      = heightCtrl_->GetValue();
    cfg.fullscreen  = fullscreenCtrl_->IsChecked();
    cfg.targetOS    = linuxRadio_->GetValue()
                          ? ConversionConfig::TargetOS::Linux
                          : ConversionConfig::TargetOS::Windows;
    return cfg;
}

void MainFrame::EnableControls(bool enable) {
    sb3PathCtrl_->Enable(enable);
    exportPathCtrl_->Enable(enable);
    titleCtrl_->Enable(enable);
    widthCtrl_->Enable(enable);
    heightCtrl_->Enable(enable);
    fullscreenCtrl_->Enable(enable);
    windowsRadio_->Enable(enable);
    linuxRadio_->Enable(enable);
    convertBtn_->Enable(enable);
}

} // namespace sc
