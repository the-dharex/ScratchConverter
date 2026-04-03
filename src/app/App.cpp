#include "app/App.hpp"
#include "ui/MainFrame.hpp"

namespace sc {

bool App::OnInit() {
    if (!wxApp::OnInit())
        return false;

    auto* frame = new MainFrame();
    frame->Show();
    return true;
}

} // namespace sc
