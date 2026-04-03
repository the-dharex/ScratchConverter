#pragma once

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include "core/ConversionConfig.hpp"

#include <memory>
#include <thread>

namespace sc {

// Custom event posted from worker thread → UI thread
wxDECLARE_EVENT(EVT_CONVERSION_UPDATE, wxThreadEvent);

class MainFrame : public wxFrame {
public:
    MainFrame();

private:
    // ── Event handlers ──
    void OnBrowseSB3(wxCommandEvent& evt);
    void OnBrowseExport(wxCommandEvent& evt);
    void OnConvert(wxCommandEvent& evt);
    void OnConversionUpdate(wxThreadEvent& evt);

    // ── Helpers ──
    void           SetStatus(const wxString& msg);
    ConversionConfig BuildConfig() const;
    void           EnableControls(bool enable);

    // ── Controls ──
    wxTextCtrl*     sb3PathCtrl_     = nullptr;
    wxTextCtrl*     exportPathCtrl_  = nullptr;
    wxTextCtrl*     titleCtrl_       = nullptr;
    wxSpinCtrl*     widthCtrl_       = nullptr;
    wxSpinCtrl*     heightCtrl_      = nullptr;
    wxCheckBox*     fullscreenCtrl_  = nullptr;
    wxRadioButton*  windowsRadio_    = nullptr;
    wxRadioButton*  linuxRadio_      = nullptr;
    wxButton*       convertBtn_      = nullptr;
    wxGauge*        progressGauge_   = nullptr;
    wxStatusBar*    statusBar_       = nullptr;

    // ── Worker ──
    std::unique_ptr<std::jthread> worker_;
    wxString lastExePath_;
};

} // namespace sc
