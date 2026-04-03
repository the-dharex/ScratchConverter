#pragma once

#include <wx/wx.h>

namespace sc {

class App : public wxApp {
public:
    bool OnInit() override;
};

} // namespace sc
