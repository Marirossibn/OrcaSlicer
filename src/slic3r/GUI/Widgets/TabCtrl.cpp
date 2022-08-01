#include "TabCtrl.hpp"

wxDEFINE_EVENT( wxEVT_TAB_SEL_CHANGING, wxCommandEvent );
wxDEFINE_EVENT( wxEVT_TAB_SEL_CHANGED, wxCommandEvent );

BEGIN_EVENT_TABLE(TabCtrl, StaticBox)

// catch paint events
END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

#define TAB_BUTTON_SPACE 2
#define TAB_BUTTON_PADDING_X 2
#define TAB_BUTTON_PADDING_Y 2
#define TAB_BUTTON_PADDING TAB_BUTTON_PADDING_X, TAB_BUTTON_PADDING_Y

TabCtrl::TabCtrl(wxWindow *      parent,
                   wxWindowID      id,
                   const wxPoint & pos,
                   const wxSize &  size,
                   long            style)
    : StaticBox(parent, id, pos, size, style)
{
#if 0
    radius = 5;
#else
    radius = 1;
#endif
    SetBorderColor(0xcecece);
    sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddSpacer(10);
    SetSizer(sizer);
    Bind(wxEVT_COMMAND_BUTTON_CLICKED, &TabCtrl::buttonClicked, this);
    //wxString reason;
    //IsTransparentBackgroundSupported(&reason);
}

TabCtrl::~TabCtrl()
{
    delete images;
}

int TabCtrl::GetSelection() const { return sel; }

void TabCtrl::SelectItem(int item)
{
    if (item == sel || !sendTabCtrlEvent(true))
        return;
    if (sel >= 0) {
        wxCommandEvent e(wxEVT_CHECKBOX);
        auto b = btns[sel];
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }
    sel = item;
    if (sel >= 0) {
        wxCommandEvent e(wxEVT_CHECKBOX);
        auto b = btns[sel];
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }
    sendTabCtrlEvent();
    relayout();
    Refresh();
}

void TabCtrl::Unselect()
{
    SelectItem(-1);
}

void TabCtrl::Rescale()
{
    for (auto & b : btns)
        b->Rescale();
}

bool TabCtrl::SetFont(wxFont const& font)
{
    StaticBox::SetFont(font);
    bold = font.Bold();
    for (size_t i = 0; i < btns.size(); ++i)
        btns[i]->SetFont(i == sel ? bold : font);
    return true;
}

int TabCtrl::AppendItem(const wxString &item,
                     int image, int selImage,
                     void * clientData)
{
    Button * btn = new Button();
    btn->Create(this, item, "", wxBORDER_NONE);
    btn->SetFont(GetFont());
    btn->SetTextColor(StateColor(
        std::make_pair(0x6B6B6B, (int) StateColor::NotChecked),
        std::make_pair(*wxLIGHT_GREY, (int) StateColor::Normal)));
    btn->SetBackgroundColor(GetBackgroundColour());
    btn->SetCornerRadius(0);
    btn->SetPaddingSize({TAB_BUTTON_PADDING});
    btns.push_back(btn);
    if (btns.size() > 1)
        sizer->GetItem(sizer->GetItemCount() - 1)->SetMinSize({0, 0});
    sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL | wxALL, TAB_BUTTON_SPACE * 2);
    sizer->AddStretchSpacer(1);
    relayout();
    return btns.size() - 1;
}

bool TabCtrl::DeleteItem(int item)
{
    return false;
}

void TabCtrl::DeleteAllItems()
{
    sizer->Clear(true);
    sizer->AddSpacer(10);
    btns.clear();
    if (sel >= 0) {
        sel = -1;
        sendTabCtrlEvent();
    }
}

unsigned int TabCtrl::GetCount() const { return btns.size(); }

wxString TabCtrl::GetItemText(unsigned int item) const
{
    return item < btns.size() ? btns[item]->GetLabel() : wxString{};
}

void TabCtrl::SetItemText(unsigned int item, wxString const &value)
{
    if (item >= btns.size()) return;
    btns[item]->SetLabel(value);
}

bool TabCtrl::GetItemBold(unsigned int item) const
{
    if (item >= btns.size()) return false;
    return btns[item]->GetFont() == bold;
}

void TabCtrl::SetItemBold(unsigned int item, bool bold)
{
    if (item >= btns.size()) return;
    btns[item]->SetFont(bold ? this->bold : GetFont());
    btns[item]->Rescale();
}

void* TabCtrl::GetItemData(unsigned int item) const
{
    if (item >= btns.size()) return nullptr;
    return btns[item]->GetClientData();
}

void TabCtrl::SetItemData(unsigned int item, void* clientData)
{
    if (item >= btns.size()) return;
    btns[item]->SetClientData(clientData);
}

void TabCtrl::AssignImageList(wxImageList* imageList)
{
    if (images == imageList) return;
    delete images;
    images = imageList;
}

void TabCtrl::SetItemTextColour(unsigned int item, const StateColor &col)
{
    if (item >= btns.size()) return;
    btns[item]->SetTextColor(col);
}

int TabCtrl::GetFirstVisibleItem() const
{
    return btns.size() == 0 ? -1 : 0;
}

int TabCtrl::GetNextVisible(int item) const
{
    return ++item < btns.size() ? item : -1;
}

bool TabCtrl::IsVisible(unsigned int item) const
{
    return true;
}

void TabCtrl::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
    if (sizeFlags & wxSIZE_USE_EXISTING) return;
    relayout();
}

void TabCtrl::relayout()
{
    int offset = 10;
    int item = sel + 1;
    int first = 0;
    for (int i = 0; i < item; ++i)
        offset += btns[i]->GetMinSize().x + TAB_BUTTON_SPACE * 2;
    if (item < btns.size())
        offset += btns[item]->GetMinSize().x + TAB_BUTTON_SPACE * 2;
    int  width = GetSize().x;
    auto sizer = GetSizer();
    for (int i = 0; i < btns.size(); ++i) {
        auto size = btns[i]->GetMinSize().x + TAB_BUTTON_SPACE * 2;
        if (i < sel && offset > width) {
            sizer->Show(i * 2 + 1, false);
            sizer->Show(i * 2 + 2, false);
            offset -= size;
            first = i + 1;
        } else if (i <= item) {
            sizer->Show(i * 2 + 1, true);
            sizer->Show(i * 2 + 2, true);
        } else if (offset <= width) {
            sizer->Show(i * 2 + 1, true);
            sizer->Show(i * 2 + 2, true);
            offset += size;
            item = i;
        } else {
            sizer->Show(i * 2 + 1, false);
            sizer->Show(i * 2 + 2, false);
        }
        sizer->GetItem(i * 2 + 2)->SetMinSize({0, 0});
    }
    if (item >= btns.size())
        -- item;
    // Keep spacing 2 ~ 10 TAB_BUTTON_SPACE
    int b = GetSize().x - offset - 10 - (item + 1 - first) * TAB_BUTTON_SPACE * 8;
    sizer->GetItem(item * 2 + 2)->SetMinSize({b > 0 ? b : 0, 0});
    sizer->Layout();
}

void TabCtrl::buttonClicked(wxCommandEvent &event)
{
    auto btn = event.GetEventObject();
    auto iter = std::find(btns.begin(), btns.end(), btn);
    SelectItem(iter == btns.end() ? -1 : iter - btns.begin());
}

void TabCtrl::doRender(wxDC& dc)
{
    wxSize size = GetSize();
    int states = state_handler.states();
    dc.SetPen(wxPen(border_color.colorForStates(states), border_width));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    
    if (sel < 0) { return; }

    auto x1 = btns[sel]->GetPosition().x;
    auto x2 = x1 + btns[sel]->GetSize().x;
    const int BS = border_width / 2;
    const int BS2 = (1 + border_width) / 2;
#if 0
    x1 -= TAB_BUTTON_SPACE; x2 += TAB_BUTTON_SPACE;
    dc.DrawLine(0, size.y - BS2, x1 - radius + BS2, size.y - BS2);
    dc.DrawArc(x1 - radius, size.y, x1, size.y - radius, x1 - radius, size.y - radius);
    dc.DrawLine(x1, size.y - radius, x1, radius);
    dc.DrawArc(x1 + radius, 0, x1, radius, x1 + radius, radius);
    dc.DrawLine(x1 + radius, BS, x2 - radius, BS);
    dc.DrawArc(x2, radius, x2 - radius, 0, x2 - radius, radius);
    dc.DrawLine(x2, radius, x2, size.y - radius);
    dc.DrawArc(x2, size.y - radius, x2 + radius, size.y, x2 + radius, size.y - radius);
    dc.DrawLine(x2 + radius - BS2, size.y - BS2, size.x, size.y - BS2);
#else
    dc.DrawLine(0, size.y - BS2, size.x, size.y - BS2);
    wxColor c(0x42AE00);
    dc.SetPen(wxPen(c, 0));
    dc.SetBrush(c);
    dc.DrawRoundedRectangle(x1 - radius, size.y - BS2 - border_width * 3, x2 + radius * 2 - x1, border_width * 3, radius);
#endif
}

bool TabCtrl::sendTabCtrlEvent(bool changing)
{
    wxCommandEvent event(changing ? wxEVT_TAB_SEL_CHANGING : wxEVT_TAB_SEL_CHANGED, GetId());
    event.SetEventObject(this);
    event.SetInt(sel);
    GetEventHandler()->ProcessEvent(event);
    return true;
}
