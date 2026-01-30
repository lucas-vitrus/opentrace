/*
 * This program source code file is part of Trace, an AI-native PCB design application.
 *
 * Copyright The Trace Developers, see TRACE_AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ai_chat_panel_base.h"
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/bmpbuttn.h>
#include <wx/tglbtn.h>
#include <wx/txtstrm.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <wx/dcclient.h>
#include <wx/settings.h>
#include <wx/dc.h>
#include <wx/event.h>
#include <wx/defs.h>
#include <wx/stattext.h>
#include <wx/statline.h>
#include <wx/menu.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <kiplatform/secrets.h>

#include <paths.h>
#include <python_manager.h>
#include <kiway_player.h>
#include <wildcards_and_files_ext.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <functional>
#include <memory>
#include <future>

#include <wx/utils.h>
#include <bitmaps.h>
#include <class_draw_panel_gal.h>
#include <eda_draw_frame.h>
#include <auth/auth_manager.h>
#include <confirm.h>
#include <ui_events.h>
#include "ai_chat_panel_base.h"
#include <ai_backend_client.h>
#include <ai_tool_executor.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <conversation_db.h>
#include <conversation_sync.h>

// Mode dropdown menu IDs
enum
{
    ID_MODE_PLAN = wxID_HIGHEST + 1,
    ID_MODE_ASK,
    ID_MODE_AGENT,
    ID_TAB_NEW,
    ID_TAB_HISTORY,
    ID_TAB_SELECT_BASE,
    ID_TAB_CLOSE_BASE = ID_TAB_SELECT_BASE + 100,
    ID_HISTORY_ITEM_BASE = ID_TAB_CLOSE_BASE + 100
};


// =========================================================================
// CONVERSATION_TAB_BAR Implementation
// =========================================================================

CONVERSATION_TAB_BAR::CONVERSATION_TAB_BAR( wxWindow* aParent, wxWindowID aId ) :
        wxPanel( aParent, aId, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_selectedTab( -1 ),
        m_hoveredTab( -1 ),
        m_hoveredClose( -1 ),
        m_hoverNewButton( false ),
        m_hoverHistoryButton( false ),
        m_scrollOffset( 0 )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    SetMinSize( wxSize( -1, 32 ) );

    Bind( wxEVT_PAINT, &CONVERSATION_TAB_BAR::onPaint, this );
    Bind( wxEVT_LEFT_DOWN, &CONVERSATION_TAB_BAR::onLeftDown, this );
    Bind( wxEVT_MOTION, &CONVERSATION_TAB_BAR::onMouseMove, this );
    Bind( wxEVT_LEAVE_WINDOW, &CONVERSATION_TAB_BAR::onMouseLeave, this );
    Bind( wxEVT_MOUSEWHEEL, &CONVERSATION_TAB_BAR::onMouseWheel, this );
}


wxSize CONVERSATION_TAB_BAR::DoGetBestSize() const
{
    return wxSize( -1, 36 );
}


int CONVERSATION_TAB_BAR::AddTab( const wxString& aConversationId, const wxString& aTitle )
{
    if( static_cast<int>( m_tabs.size() ) >= MAX_TABS )
        return -1;

    TabInfo tab;
    tab.conversationId = aConversationId;
    tab.title = aTitle.IsEmpty() ? wxString( wxT( "New Chat" ) ) : aTitle;
    m_tabs.push_back( tab );

    int newIndex = static_cast<int>( m_tabs.size() ) - 1;
    SelectTab( newIndex );
    
    // Force size recalculation and relayout of entire parent hierarchy
    InvalidateBestSize();
    SetMinSize( DoGetBestSize() );  // Ensure minimum size is respected
    if( GetParent() )
        GetParent()->Layout();
    
    return newIndex;
}


void CONVERSATION_TAB_BAR::RemoveTab( int aIndex )
{
    if( aIndex < 0 || aIndex >= static_cast<int>( m_tabs.size() ) )
        return;

    m_tabs.erase( m_tabs.begin() + aIndex );

    // Adjust selection
    if( m_selectedTab >= static_cast<int>( m_tabs.size() ) )
        m_selectedTab = static_cast<int>( m_tabs.size() ) - 1;

    // Force size recalculation and relayout of entire parent hierarchy
    InvalidateBestSize();
    SetMinSize( DoGetBestSize() );  // Ensure minimum size is respected
    if( GetParent() )
        GetParent()->Layout();

    // Fire event to notify parent
    wxCommandEvent evt( wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGED, GetId() );
    evt.SetInt( m_selectedTab );
    evt.SetEventObject( this );
    ProcessEvent( evt );
}


void CONVERSATION_TAB_BAR::SelectTab( int aIndex )
{
    if( aIndex < 0 || aIndex >= static_cast<int>( m_tabs.size() ) )
        return;

    if( m_selectedTab != aIndex )
    {
        m_selectedTab = aIndex;
        Refresh();

        // Fire selection event
        wxCommandEvent evt( wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGED, GetId() );
        evt.SetInt( m_selectedTab );
        evt.SetEventObject( this );
        ProcessEvent( evt );
    }
}


wxString CONVERSATION_TAB_BAR::GetTabConversationId( int aIndex ) const
{
    if( aIndex >= 0 && aIndex < static_cast<int>( m_tabs.size() ) )
        return m_tabs[aIndex].conversationId;
    return wxEmptyString;
}


void CONVERSATION_TAB_BAR::SetTabTitle( int aIndex, const wxString& aTitle )
{
    if( aIndex >= 0 && aIndex < static_cast<int>( m_tabs.size() ) )
    {
        m_tabs[aIndex].title = aTitle;
        Refresh();
    }
}


wxRect CONVERSATION_TAB_BAR::getTabRect( int aIndex ) const
{
    const int tabHeight = 28;
    const int tabSpacing = 4;
    const int leftMargin = 8;
    const int topMargin = 4;
    const int tabWidth = 120;

    int x = leftMargin + aIndex * ( tabWidth + tabSpacing ) - m_scrollOffset;
    return wxRect( x, topMargin, tabWidth, tabHeight );
}


int CONVERSATION_TAB_BAR::getTotalTabsWidth() const
{
    const int tabWidth = 120;
    const int tabSpacing = 4;
    const int leftMargin = 8;
    const int rightButtons = 70;  // + button and history button
    
    int numTabs = static_cast<int>( m_tabs.size() );
    if( numTabs == 0 )
        return leftMargin + rightButtons;
    
    return leftMargin + numTabs * tabWidth + ( numTabs - 1 ) * tabSpacing + rightButtons;
}


void CONVERSATION_TAB_BAR::onMouseWheel( wxMouseEvent& aEvent )
{
    int rotation = aEvent.GetWheelRotation();
    if( rotation == 0 )
        return;
    
    // Low sensitivity: scroll 30px per wheel notch (instead of 60)
    const int scrollStep = 30;
    // Natural scroll: negative rotation (swipe left) scrolls left (decrease offset)
    m_scrollOffset += ( rotation < 0 ? -1 : 1 ) * scrollStep;
    
    // Clamp scroll offset
    int totalWidth = getTotalTabsWidth();
    int availableWidth = GetClientSize().GetWidth();
    int maxScroll = std::max( 0, totalWidth - availableWidth );
    m_scrollOffset = std::max( 0, std::min( m_scrollOffset, maxScroll ) );
    
    Refresh();
}


wxRect CONVERSATION_TAB_BAR::getCloseButtonRect( int aIndex ) const
{
    wxRect tabRect = getTabRect( aIndex );
    const int btnSize = 16;
    const int margin = 4;

    return wxRect( tabRect.GetRight() - btnSize - margin,
                   tabRect.GetY() + ( tabRect.GetHeight() - btnSize ) / 2,
                   btnSize, btnSize );
}


wxRect CONVERSATION_TAB_BAR::getNewButtonRect() const
{
    const int btnSize = 24;
    const int topMargin = 4;

    // Position at the right side, before history button (fixed position, doesn't scroll)
    int x = GetClientSize().GetWidth() - 62;
    
    return wxRect( x, topMargin + 2, btnSize, btnSize );
}


wxRect CONVERSATION_TAB_BAR::getHistoryButtonRect() const
{
    wxRect newBtnRect = getNewButtonRect();
    const int btnSize = 24;

    return wxRect( newBtnRect.GetRight() + 8, newBtnRect.GetY(), btnSize, btnSize );
}


void CONVERSATION_TAB_BAR::onPaint( wxPaintEvent& aEvent )
{
    wxPaintDC dc( this );
    wxRect    rect = GetClientRect();

    // Modern 2025 color scheme - get system colors and derive accents
    wxColour bgColor = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
    wxColour fgColor = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
    wxColour highlightColor = wxSystemSettings::GetColour( wxSYS_COLOUR_HIGHLIGHT );
    wxColour borderColor = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNSHADOW );

    // Calculate if we're in dark mode
    int luminance = ( bgColor.Red() * 299 + bgColor.Green() * 587 + bgColor.Blue() * 114 ) / 1000;
    bool isDark = luminance < 128;

    // Modern subtle background
    wxColour tabBarBg = isDark ? bgColor.ChangeLightness( 105 ) : bgColor.ChangeLightness( 98 );

    // Fill background with subtle gradient effect
    dc.SetBrush( wxBrush( tabBarBg ) );
    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.DrawRectangle( rect );

    // Draw subtle bottom border
    wxColour bottomBorder = isDark ? borderColor.ChangeLightness( 80 ) : borderColor.ChangeLightness( 120 );
    dc.SetPen( wxPen( bottomBorder ) );
    dc.DrawLine( 0, rect.GetHeight() - 1, rect.GetWidth(), rect.GetHeight() - 1 );

    // Modern font setup
    wxFont tabFont = GetFont();
    tabFont.SetPointSize( tabFont.GetPointSize() - 1 );
    dc.SetFont( tabFont );

    // Draw tabs with modern styling
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        wxRect tabRect = getTabRect( static_cast<int>( i ) );
        bool   isSelected = ( static_cast<int>( i ) == m_selectedTab );
        bool   isHovered = ( static_cast<int>( i ) == m_hoveredTab );

        // Modern tab background with subtle gradients
        wxColour tabBg;
        wxColour tabBorder;
        if( isSelected )
        {
            tabBg = highlightColor;
            tabBorder = highlightColor.ChangeLightness( isDark ? 120 : 80 );
        }
        else if( isHovered )
        {
            tabBg = isDark ? borderColor.ChangeLightness( 130 ) : borderColor.ChangeLightness( 140 );
            tabBorder = tabBg.ChangeLightness( isDark ? 90 : 80 );
        }
        else
        {
            tabBg = isDark ? bgColor.ChangeLightness( 115 ) : bgColor.ChangeLightness( 96 );
            tabBorder = tabBg.ChangeLightness( isDark ? 80 : 90 );
        }

        dc.SetBrush( wxBrush( tabBg ) );
        dc.SetPen( wxPen( tabBorder ) );
        dc.DrawRoundedRectangle( tabRect, 6 );  // Slightly more rounded corners

        // Tab title with better typography
        wxColour textColor = isSelected ? *wxWHITE : fgColor;
        if( isHovered && !isSelected )
            textColor = isDark ? fgColor.ChangeLightness( 120 ) : fgColor.ChangeLightness( 80 );

        dc.SetTextForeground( textColor );
        wxString title = m_tabs[i].title;
        if( title.length() > 12 )
            title = title.Left( 10 ) + wxString( wxT( "..." ) );

        wxCoord textWidth, textHeight;
        dc.GetTextExtent( title, &textWidth, &textHeight );
        int textX = tabRect.GetX() + 10;  // More padding
        int textY = tabRect.GetY() + ( tabRect.GetHeight() - textHeight ) / 2;
        dc.DrawText( title, textX, textY );

        // Modern close button (X) with hover state
        wxRect closeRect = getCloseButtonRect( static_cast<int>( i ) );
        bool   closeHovered = ( static_cast<int>( i ) == m_hoveredClose );

        if( closeHovered )
        {
            // Red-ish hover for close button
            wxColour closeBg = isDark ? wxColour( 180, 60, 60 ) : wxColour( 220, 100, 100 );
            dc.SetBrush( wxBrush( closeBg ) );
            dc.SetPen( *wxTRANSPARENT_PEN );
            dc.DrawRoundedRectangle( closeRect, 4 );
        }

        // Draw X with better visibility
        wxColour xColor = closeHovered ? *wxWHITE : ( isSelected ? wxColour( 200, 200, 200 ) : fgColor.ChangeLightness( 150 ) );
        dc.SetPen( wxPen( xColor, closeHovered ? 2 : 1 ) );
        int cx = closeRect.GetX() + closeRect.GetWidth() / 2;
        int cy = closeRect.GetY() + closeRect.GetHeight() / 2;
        int d = 3;
        dc.DrawLine( cx - d, cy - d, cx + d + 1, cy + d + 1 );
        dc.DrawLine( cx + d, cy - d, cx - d - 1, cy + d + 1 );
    }

    // Modern + button
    if( static_cast<int>( m_tabs.size() ) < MAX_TABS )
    {
        wxRect newBtnRect = getNewButtonRect();

        wxColour btnBg = m_hoverNewButton
                             ? ( isDark ? highlightColor.ChangeLightness( 90 ) : highlightColor.ChangeLightness( 110 ) )
                             : ( isDark ? bgColor.ChangeLightness( 115 ) : bgColor.ChangeLightness( 96 ) );
        wxColour btnBorder = btnBg.ChangeLightness( isDark ? 80 : 90 );

        dc.SetBrush( wxBrush( btnBg ) );
        dc.SetPen( wxPen( btnBorder ) );
        dc.DrawRoundedRectangle( newBtnRect, 6 );

        // Draw + symbol
        wxColour plusColor = m_hoverNewButton ? *wxWHITE : fgColor;
        dc.SetPen( wxPen( plusColor, 2 ) );
        int cx = newBtnRect.GetX() + newBtnRect.GetWidth() / 2;
        int cy = newBtnRect.GetY() + newBtnRect.GetHeight() / 2;
        int d = 5;
        dc.DrawLine( cx - d, cy, cx + d + 1, cy );
        dc.DrawLine( cx, cy - d, cx, cy + d + 1 );
    }

    // Modern history button
    wxRect histBtnRect = getHistoryButtonRect();

    wxColour histBg = m_hoverHistoryButton
                          ? ( isDark ? highlightColor.ChangeLightness( 90 ) : highlightColor.ChangeLightness( 110 ) )
                          : ( isDark ? bgColor.ChangeLightness( 115 ) : bgColor.ChangeLightness( 96 ) );
    wxColour histBorder = histBg.ChangeLightness( isDark ? 80 : 90 );

    dc.SetBrush( wxBrush( histBg ) );
    dc.SetPen( wxPen( histBorder ) );
    dc.DrawRoundedRectangle( histBtnRect, 6 );

    // Draw clock/history icon
    wxColour iconColor = m_hoverHistoryButton ? *wxWHITE : fgColor;
    dc.SetPen( wxPen( iconColor, 1 ) );
    dc.SetBrush( *wxTRANSPARENT_BRUSH );
    int hcx = histBtnRect.GetX() + histBtnRect.GetWidth() / 2;
    int hcy = histBtnRect.GetY() + histBtnRect.GetHeight() / 2;
    dc.DrawCircle( hcx, hcy, 6 );
    dc.DrawLine( hcx, hcy - 3, hcx, hcy );
    dc.DrawLine( hcx, hcy, hcx + 2, hcy + 2 );
    
}


void CONVERSATION_TAB_BAR::onLeftDown( wxMouseEvent& aEvent )
{
    wxPoint pos = aEvent.GetPosition();

    // Check close buttons first
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        if( getCloseButtonRect( static_cast<int>( i ) ).Contains( pos ) )
        {
            // Fire close event
            wxCommandEvent evt( wxEVT_BUTTON, ID_TAB_CLOSE_BASE + static_cast<int>( i ) );
            evt.SetInt( static_cast<int>( i ) );
            evt.SetEventObject( this );
            wxPostEvent( GetParent(), evt );
            return;
        }
    }

    // Check tab clicks
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        if( getTabRect( static_cast<int>( i ) ).Contains( pos ) )
        {
            SelectTab( static_cast<int>( i ) );
            return;
        }
    }

    // Check + button
    if( static_cast<int>( m_tabs.size() ) < MAX_TABS && getNewButtonRect().Contains( pos ) )
    {
        wxCommandEvent evt( wxEVT_BUTTON, ID_TAB_NEW );
        evt.SetEventObject( this );
        wxPostEvent( GetParent(), evt );
        return;
    }

    // Check history button
    if( getHistoryButtonRect().Contains( pos ) )
    {
        wxCommandEvent evt( wxEVT_BUTTON, ID_TAB_HISTORY );
        evt.SetEventObject( this );
        wxPostEvent( GetParent(), evt );
        return;
    }
}


void CONVERSATION_TAB_BAR::onMouseMove( wxMouseEvent& aEvent )
{
    wxPoint pos = aEvent.GetPosition();
    int     oldHoveredTab = m_hoveredTab;
    int     oldHoveredClose = m_hoveredClose;
    bool    oldHoverNew = m_hoverNewButton;
    bool    oldHoverHistory = m_hoverHistoryButton;

    m_hoveredTab = -1;
    m_hoveredClose = -1;
    m_hoverNewButton = false;
    m_hoverHistoryButton = false;

    // Check tabs
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        if( getTabRect( static_cast<int>( i ) ).Contains( pos ) )
        {
            m_hoveredTab = static_cast<int>( i );

            if( getCloseButtonRect( static_cast<int>( i ) ).Contains( pos ) )
                m_hoveredClose = static_cast<int>( i );
            break;
        }
    }

    // Check + button
    if( static_cast<int>( m_tabs.size() ) < MAX_TABS )
        m_hoverNewButton = getNewButtonRect().Contains( pos );

    // Check history button
    m_hoverHistoryButton = getHistoryButtonRect().Contains( pos );

    // Update cursor based on what's being hovered
    bool isOverClickable = ( m_hoveredTab >= 0 || m_hoverNewButton || m_hoverHistoryButton );
    SetCursor( isOverClickable ? wxCursor( wxCURSOR_HAND ) : wxNullCursor );

    if( oldHoveredTab != m_hoveredTab || oldHoveredClose != m_hoveredClose ||
        oldHoverNew != m_hoverNewButton || oldHoverHistory != m_hoverHistoryButton )
    {
        Refresh();
    }
}


void CONVERSATION_TAB_BAR::onMouseLeave( wxMouseEvent& aEvent )
{
    SetCursor( wxNullCursor );  // Reset cursor when leaving
    m_hoveredTab = -1;
    m_hoveredClose = -1;
    m_hoverNewButton = false;
    m_hoverHistoryButton = false;
    Refresh();
}


MODE_DROPDOWN_BUTTON::MODE_DROPDOWN_BUTTON( wxWindow* aParent, wxWindowID aId, const wxPoint& aPos,
                                            const wxSize& aSize ) :
        wxPanel( aParent, aId, aPos, aSize, wxBORDER_NONE ),
        m_currentMode( AI_MODE::AGENT ),
        m_isHovered( false )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    SetCanFocus( true );

    wxFont buttonFont = GetFont();
    if( buttonFont.IsOk() )
    {
        buttonFont.SetPointSize( buttonFont.GetPointSize() - 1 );
        SetFont( buttonFont );
    }

    Bind( wxEVT_PAINT, &MODE_DROPDOWN_BUTTON::onPaint, this );
    Bind( wxEVT_LEFT_UP, &MODE_DROPDOWN_BUTTON::onLeftUp, this );
    Bind( wxEVT_ENTER_WINDOW, &MODE_DROPDOWN_BUTTON::onMouseEnter, this );
    Bind( wxEVT_LEAVE_WINDOW, &MODE_DROPDOWN_BUTTON::onMouseLeave, this );
    Bind( wxEVT_ERASE_BACKGROUND,
          []( wxEraseEvent& aEvent )
          {
              aEvent.Skip( false );
          } );
}


wxSize MODE_DROPDOWN_BUTTON::DoGetBestSize() const
{
    wxClientDC dc( const_cast<MODE_DROPDOWN_BUTTON*>( this ) );
    dc.SetFont( GetFont() );

    // Calculate size based on longest possible label + chevron space
    wxString longestLabel = "Agent"; // Longest mode label
    wxSize   textSize = dc.GetTextExtent( longestLabel );

    const int paddingX = 12;
    const int paddingY = 6;
    const int chevronSpace = 20; // Space for chevron on the right
    wxSize    buttonSize( textSize.x + ( paddingX * 2 ) + chevronSpace, textSize.y + ( paddingY * 2 ) );

    return buttonSize;
}


void MODE_DROPDOWN_BUTTON::SetMode( AI_MODE aMode )
{
    if( m_currentMode != aMode )
    {
        m_currentMode = aMode;
        Refresh();

        // Fire wxEVT_CHOICE event to notify parent
        wxCommandEvent evt( wxEVT_CHOICE, GetId() );
        evt.SetEventObject( this );
        ProcessEvent( evt );
    }
}


wxString MODE_DROPDOWN_BUTTON::GetModeLabel() const
{
    switch( m_currentMode )
    {
    case AI_MODE::PLAN: return "Plan";
    case AI_MODE::ASK: return "Ask";
    case AI_MODE::AGENT: return "Agent";
    default: return "Ask";
    }
}


void MODE_DROPDOWN_BUTTON::onPaint( wxPaintEvent& aEvent )
{
    wxPaintDC dc( this );
    wxRect    rect = GetClientRect();

#ifdef __WXMSW__
    // Fill entire background with parent's background color first
    // This prevents dark patches around rounded corners on Windows
    wxColour parentBg = GetParent()->GetBackgroundColour();
    dc.SetBrush( wxBrush( parentBg ) );
    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.DrawRectangle( rect );
#endif

    bool isDark = KIPLATFORM::UI::IsDarkTheme();

    wxColour bgColor;
    wxColour borderColor;
    wxColour textColor;

    // Style similar to unselected toggle button
    wxColour baseColor = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE );
    if( isDark )
    {
        bgColor = baseColor.ChangeLightness( 110 );
        borderColor = baseColor.ChangeLightness( 120 );
        textColor = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT );
    }
    else
    {
        bgColor = baseColor.ChangeLightness( 100 );
        borderColor = baseColor.ChangeLightness( 85 );
        textColor = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT );
    }

    if( m_isHovered )
    {
        bgColor = bgColor.ChangeLightness( isDark ? 115 : 105 );
    }

    dc.SetBrush( wxBrush( bgColor ) );
    dc.SetPen( wxPen( borderColor, 1 ) );
    dc.DrawRoundedRectangle( rect, 12 );

    dc.SetFont( GetFont() );
    dc.SetTextForeground( textColor );

    // Draw label
    wxString label = GetModeLabel();
    wxSize   textSize = dc.GetTextExtent( label );

    // Draw text on the left side with padding
    int textX = 12;
    int textY = ( rect.height - textSize.y ) / 2;
    dc.DrawText( label, wxPoint( textX, textY ) );

    // Draw chevron/caret on the right side
    int chevronX = rect.width - 16;
    int chevronY = rect.height / 2;
    int chevronSize = 4;

    dc.SetPen( wxPen( textColor, 2 ) );
    dc.SetBrush( *wxTRANSPARENT_BRUSH );

    // Draw down-pointing chevron (two lines forming a V)
    wxPoint chevronPoints[3];
    chevronPoints[0] = wxPoint( chevronX - chevronSize, chevronY - 2 );
    chevronPoints[1] = wxPoint( chevronX, chevronY + 2 );
    chevronPoints[2] = wxPoint( chevronX + chevronSize, chevronY - 2 );

    dc.DrawLines( 3, chevronPoints );

    aEvent.Skip( false );
}


void MODE_DROPDOWN_BUTTON::onLeftUp( wxMouseEvent& aEvent )
{
    showPopupMenu();
    aEvent.Skip();
}


void MODE_DROPDOWN_BUTTON::onMouseEnter( wxMouseEvent& aEvent )
{
    m_isHovered = true;
    SetCursor( wxCursor( wxCURSOR_HAND ) );
    Refresh();
    aEvent.Skip();
}


void MODE_DROPDOWN_BUTTON::onMouseLeave( wxMouseEvent& aEvent )
{
    m_isHovered = false;
    SetCursor( wxNullCursor );
    Refresh();
    aEvent.Skip();
}


void MODE_DROPDOWN_BUTTON::showPopupMenu()
{
    wxMenu menu;

    // Add menu items as radio buttons (Plan mode disabled for now)
    wxMenuItem* askItem = menu.AppendRadioItem( ID_MODE_ASK, "Ask" );
    wxMenuItem* agentItem = menu.AppendRadioItem( ID_MODE_AGENT, "Agent" );

    // Check current mode
    switch( m_currentMode )
    {
    case AI_MODE::PLAN: // Fall through to ASK if somehow set to PLAN
    case AI_MODE::ASK: askItem->Check(); break;
    case AI_MODE::AGENT: agentItem->Check(); break;
    }

    // Bind menu events
    Bind( wxEVT_MENU, &MODE_DROPDOWN_BUTTON::onMenuSelect, this, ID_MODE_ASK );
    Bind( wxEVT_MENU, &MODE_DROPDOWN_BUTTON::onMenuSelect, this, ID_MODE_AGENT );

    // Estimate menu height (2 items, minimal padding)
    int estimatedMenuHeight = 55;

    // Position menu above the button
    wxPoint menuPos( 0, -estimatedMenuHeight );

    // Show menu above the button
    PopupMenu( &menu, menuPos );

    // Unbind after use
    Unbind( wxEVT_MENU, &MODE_DROPDOWN_BUTTON::onMenuSelect, this, ID_MODE_ASK );
    Unbind( wxEVT_MENU, &MODE_DROPDOWN_BUTTON::onMenuSelect, this, ID_MODE_AGENT );
}


void MODE_DROPDOWN_BUTTON::onMenuSelect( wxCommandEvent& aEvent )
{
    int id = aEvent.GetId();

    switch( id )
    {
    case ID_MODE_ASK: SetMode( AI_MODE::ASK ); break;
    case ID_MODE_AGENT: SetMode( AI_MODE::AGENT ); break;
    }
}


STYLED_TOGGLE_BUTTON::STYLED_TOGGLE_BUTTON( wxWindow* aParent, wxWindowID aId, const wxString& aLabel,
                                           const wxPoint& aPos, const wxSize& aSize, long aStyle ) :
        wxPanel( aParent, aId, aPos, aSize, wxBORDER_NONE | aStyle ),
        m_isSelected( false ),
        m_isHovered( false ),
        m_label( aLabel )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    SetCanFocus( true );
    
    wxFont buttonFont = GetFont();
    if( buttonFont.IsOk() )
    {
        buttonFont.SetPointSize( buttonFont.GetPointSize() - 1 );
        SetFont( buttonFont );
    }
    
    Bind( wxEVT_PAINT, &STYLED_TOGGLE_BUTTON::onPaint, this );
    Bind( wxEVT_LEFT_DOWN, &STYLED_TOGGLE_BUTTON::onLeftDown, this );
    Bind( wxEVT_LEFT_UP, &STYLED_TOGGLE_BUTTON::onLeftUp, this );
    Bind( wxEVT_ENTER_WINDOW, &STYLED_TOGGLE_BUTTON::onMouseEnter, this );
    Bind( wxEVT_LEAVE_WINDOW, &STYLED_TOGGLE_BUTTON::onMouseLeave, this );
    Bind( wxEVT_ERASE_BACKGROUND,
          []( wxEraseEvent& aEvent )
          {
              aEvent.Skip( false );
          } );
}


wxSize STYLED_TOGGLE_BUTTON::DoGetBestSize() const
{
    wxClientDC dc( const_cast<STYLED_TOGGLE_BUTTON*>( this ) );
    dc.SetFont( GetFont() );
    wxSize textSize = dc.GetTextExtent( m_label );

    const int paddingX = 12;
    const int paddingY = 6;
    wxSize    buttonSize( textSize.x + ( paddingX * 2 ), textSize.y + ( paddingY * 2 ) );
    
    return buttonSize;
}


void STYLED_TOGGLE_BUTTON::SetValue( bool aValue )
{
    if( m_isSelected != aValue )
    {
        m_isSelected = aValue;
        Refresh();
        
        wxCommandEvent evt( wxEVT_TOGGLEBUTTON, GetId() );
        evt.SetEventObject( this );
        evt.SetInt( m_isSelected ? 1 : 0 );
        ProcessEvent( evt );
    }
}


void STYLED_TOGGLE_BUTTON::onPaint( wxPaintEvent& aEvent )
{
    wxPaintDC dc( this );
    wxRect    rect = GetClientRect();

#ifdef __WXMSW__
    // Fill entire background with parent's background color first
    // This prevents dark patches around rounded corners on Windows
    wxColour parentBg = GetParent()->GetBackgroundColour();
    dc.SetBrush( wxBrush( parentBg ) );
    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.DrawRectangle( rect );
#endif

    bool isDark = KIPLATFORM::UI::IsDarkTheme();

    wxColour bgColor;
    wxColour borderColor;
    wxColour textColor;

    if( m_isSelected )
    {
        if( isDark )
        {
            bgColor = wxColour( 0, 120, 255 );
            borderColor = wxColour( 0, 140, 255 );
            textColor = wxColour( 255, 255, 255 );
        }
        else
        {
            bgColor = wxColour( 0, 100, 200 );
            borderColor = wxColour( 0, 80, 180 );
            textColor = wxColour( 255, 255, 255 );
        }
    }
    else
    {
        wxColour baseColor = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE );
        if( isDark )
        {
            bgColor = baseColor.ChangeLightness( 110 );
            borderColor = baseColor.ChangeLightness( 120 );
            textColor = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT );
        }
        else
        {
            bgColor = baseColor.ChangeLightness( 100 );
            borderColor = baseColor.ChangeLightness( 85 );
            textColor = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT );
        }
    }

    if( m_isHovered && !m_isSelected )
    {
        bgColor = bgColor.ChangeLightness( isDark ? 115 : 105 );
    }

    dc.SetBrush( wxBrush( bgColor ) );
    dc.SetPen( wxPen( borderColor, 1 ) );
    dc.DrawRoundedRectangle( rect, 12 );

    dc.SetFont( GetFont() );
    dc.SetTextForeground( textColor );
    if( !m_label.IsEmpty() )
    {
        wxSize  textSize = dc.GetTextExtent( m_label );
        wxPoint textPos( ( rect.width - textSize.x ) / 2, ( rect.height - textSize.y ) / 2 );
        dc.DrawText( m_label, textPos );
    }

    aEvent.Skip( false );
}


void STYLED_TOGGLE_BUTTON::onLeftDown( wxMouseEvent& aEvent )
{
    CaptureMouse();
    aEvent.Skip();
}


void STYLED_TOGGLE_BUTTON::onLeftUp( wxMouseEvent& aEvent )
{
    if( HasCapture() )
    {
        ReleaseMouse();
        
        wxRect rect = GetClientRect();
        if( rect.Contains( aEvent.GetPosition() ) )
        {
            SetValue( !m_isSelected );
        }
    }
    aEvent.Skip();
}


void STYLED_TOGGLE_BUTTON::onMouseEnter( wxMouseEvent& aEvent )
{
    m_isHovered = true;
    Refresh();
    aEvent.Skip();
}


void STYLED_TOGGLE_BUTTON::onMouseLeave( wxMouseEvent& aEvent )
{
    m_isHovered = false;
    Refresh();
    aEvent.Skip();
}


// =============================================================================
// TAB_CONTENT_PANEL Implementation
// =============================================================================

TAB_CONTENT_PANEL::TAB_CONTENT_PANEL( wxWindow* aParent, wxWindowID aId ) :
    wxPanel( aParent, aId, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
    m_chatHistory( nullptr )
{
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    
    // Create the chat history panel for this tab
    m_chatHistory = new CHAT_MESSAGE_PANEL( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL );
    sizer->Add( m_chatHistory, 1, wxEXPAND );
    
    SetSizer( sizer );
    SetBackgroundColour( KIPLATFORM::UI::IsDarkTheme() ? wxColour( 30, 30, 30 ) : wxColour( 250, 250, 250 ) );
}

void TAB_CONTENT_PANEL::ShowTypingIndicator()
{
    if( m_chatHistory )
        m_chatHistory->ShowTypingIndicator();
}

void TAB_CONTENT_PANEL::HideTypingIndicator()
{
    if( m_chatHistory )
        m_chatHistory->HideTypingIndicator();
}

void TAB_CONTENT_PANEL::ShowStatusIndicator( const wxString& aStatus )
{
    if( m_chatHistory )
        m_chatHistory->ShowStatusIndicator( aStatus );
}

void TAB_CONTENT_PANEL::HideStatusIndicator()
{
    if( m_chatHistory )
        m_chatHistory->HideStatusIndicator();
}

void TAB_CONTENT_PANEL::ShowLoadingSkeleton()
{
    if( m_chatHistory )
        m_chatHistory->ShowLoadingSkeleton();
}

void TAB_CONTENT_PANEL::HideLoadingSkeleton()
{
    if( m_chatHistory )
        m_chatHistory->HideLoadingSkeleton();
}

void TAB_CONTENT_PANEL::Clear()
{
    if( m_chatHistory )
        m_chatHistory->Clear();
}

void TAB_CONTENT_PANEL::AddUserMessage( const wxString& aMessage )
{
    if( m_chatHistory )
        m_chatHistory->AddUserMessage( aMessage );
}

void TAB_CONTENT_PANEL::AddAIMessage( const wxString& aMessage, bool aIsHtml )
{
    if( m_chatHistory )
        m_chatHistory->AddAIMessage( aMessage, aIsHtml );
}

void TAB_CONTENT_PANEL::AppendToLastAIMessage( const wxString& aText )
{
    if( m_chatHistory )
        m_chatHistory->AppendToLastAIMessage( aText );
}


STYLED_MULTILINE_TEXTCTRL::STYLED_MULTILINE_TEXTCTRL( wxWindow* aParent, wxWindowID aId, const wxPoint& aPos,
                                                      const wxSize& aSize, long aStyle ) :
        wxPanel( aParent, aId, aPos, aSize, wxBORDER_NONE | aStyle ),
        m_textCtrl( nullptr ),
        m_minHeight( 0 ),
        m_maxHeight( 0 ),
        m_borderRadius( 5 ),
        m_padding( 4 ),
        m_isAdjusting( false ),
        m_adjustHeightTimer( nullptr )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );

    wxClientDC dc( this );
    dc.SetFont( GetFont() );
    wxSize textExtent = dc.GetTextExtent( wxT( "Ag" ) );
    int    lineHeight = textExtent.y;

    m_minHeight = lineHeight + ( m_padding * 2 ) + 2;
    m_maxHeight = ( lineHeight * 5 ) + ( m_padding * 2 ) + 2;

    SetMinSize( wxSize( -1, m_minHeight ) );
    
    wxSize initialSize = aSize;
    if( initialSize == wxDefaultSize )
    {
        initialSize = wxSize( 200, m_minHeight );
    }
    else
    {
        if( initialSize.y < m_minHeight )
            initialSize.y = m_minHeight;
        if( initialSize.y > m_maxHeight )
            initialSize.y = m_maxHeight;
    }
    
    SetSize( initialSize );

    int textCtrlWidth = initialSize.x - ( m_padding * 2 );
    int textCtrlHeight = initialSize.y - ( m_padding * 2 );
    if( textCtrlWidth < 0 )
        textCtrlWidth = 0;
    if( textCtrlHeight < 0 )
        textCtrlHeight = 0;
    
#ifdef __WXMSW__
    // Windows: Use native multiline with proper scrolling and rich text support
    // wxTE_RICH2: Enables proper multiline, copy/paste, and text rendering on Windows
    // wxTE_WORDWRAP: Forces immediate word wrapping (prevents horizontal scroll bug)
    // Removed wxTE_NO_VSCROLL to allow proper multiline expansion and scrolling
    m_textCtrl = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxPoint( m_padding, m_padding ),
                                 wxSize( textCtrlWidth, textCtrlHeight ),
                                 wxBORDER_NONE | wxTE_MULTILINE | wxTE_WORDWRAP | wxTE_RICH2 );
    
    // Windows-specific: Set immediate wrapping mode to prevent horizontal scroll
    // This fixes the bug where text goes right before wrapping to next line
    if( m_textCtrl )
    {
        m_textCtrl->SetWindowStyleFlag( m_textCtrl->GetWindowStyleFlag() & ~wxHSCROLL );
    }
#else
    // Mac/Linux: Keep existing configuration (works well there)
    m_textCtrl = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxPoint( m_padding, m_padding ),
                                 wxSize( textCtrlWidth, textCtrlHeight ),
                                 wxBORDER_NONE | wxTE_WORDWRAP | wxTE_NO_VSCROLL | wxTE_MULTILINE | wxTE_BESTWRAP );
#endif

    Bind( wxEVT_PAINT, &STYLED_MULTILINE_TEXTCTRL::onPaint, this );
    if( m_textCtrl )
    {
        m_textCtrl->Bind( wxEVT_CHAR_HOOK, &STYLED_MULTILINE_TEXTCTRL::onCharHook, this );
    }
    Bind( wxEVT_SIZE, &STYLED_MULTILINE_TEXTCTRL::onSize, this );
    Bind( wxEVT_ERASE_BACKGROUND,
          []( wxEraseEvent& aEvent )
          {
              aEvent.Skip( false );
          } );

    if( m_textCtrl )
    {
        m_textCtrl->Bind( wxEVT_TEXT, &STYLED_MULTILINE_TEXTCTRL::onTextChange, this );
        m_textCtrl->Bind( wxEVT_KILL_FOCUS, &STYLED_MULTILINE_TEXTCTRL::onKillFocus, this );
    }

    // Create and bind height adjustment timer for debouncing
    m_adjustHeightTimer = new wxTimer( this, wxID_ANY );
    Bind( wxEVT_TIMER, &STYLED_MULTILINE_TEXTCTRL::onAdjustHeightTimer, this, m_adjustHeightTimer->GetId() );
}


wxSize STYLED_MULTILINE_TEXTCTRL::DoGetBestSize() const
{
    return wxSize( -1, m_minHeight );
}


wxString STYLED_MULTILINE_TEXTCTRL::GetValue() const
{
    if( m_textCtrl )
        return m_textCtrl->GetValue();
    return wxEmptyString;
}


void STYLED_MULTILINE_TEXTCTRL::SetValue( const wxString& aValue )
{
    if( m_textCtrl )
    {
        m_textCtrl->SetValue( aValue );
        adjustHeight();
    }
}


void STYLED_MULTILINE_TEXTCTRL::Clear()
{
    if( m_textCtrl )
    {
        m_textCtrl->Clear();
        adjustHeight();
    }
}


void STYLED_MULTILINE_TEXTCTRL::SetFocus()
{
    if( m_textCtrl )
        m_textCtrl->SetFocus();
}


bool STYLED_MULTILINE_TEXTCTRL::Enable( bool aEnable )
{
    bool result = wxPanel::Enable( aEnable );
    if( m_textCtrl )
        m_textCtrl->Enable( aEnable );
    return result;
}


bool STYLED_MULTILINE_TEXTCTRL::IsEnabled() const
{
    return wxPanel::IsEnabled();
}


void STYLED_MULTILINE_TEXTCTRL::onPaint( wxPaintEvent& aEvent )
{
    wxPaintDC dc( this );
    wxRect    rect = GetClientRect();
#ifdef __WXMSW__
    // Fill entire background with parent's background color first
    // This prevents dark patches around rounded corners on Windows
    wxColour parentBg = GetParent()->GetBackgroundColour();
    dc.SetBrush( wxBrush( parentBg ) );
    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.DrawRectangle( rect );
#endif

    bool isDark = KIPLATFORM::UI::IsDarkTheme();

    wxColour bgColor = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
    wxColour borderColor;
    
    if( isDark )
    {
        borderColor = bgColor.ChangeLightness( 130 );
    }
    else
    {
        borderColor = bgColor.ChangeLightness( 80 );
    }

    dc.SetBrush( wxBrush( bgColor ) );
    dc.SetPen( wxPen( borderColor, 1 ) );
    dc.DrawRoundedRectangle( rect, m_borderRadius );

    aEvent.Skip( false );
}


void STYLED_MULTILINE_TEXTCTRL::onCharHook( wxKeyEvent& aEvent )
{
    int keyCode = aEvent.GetKeyCode();

    if( keyCode == WXK_RETURN || keyCode == WXK_NUMPAD_ENTER )
    {
        if( aEvent.ShiftDown() )
        {
            aEvent.Skip();
        }
        else
        {
            // Flush any pending height adjustment before sending Enter event
            if( m_adjustHeightTimer && m_adjustHeightTimer->IsRunning() )
            {
                m_adjustHeightTimer->Stop();
                adjustHeight();
            }
            aEvent.Skip( false );
            wxCommandEvent sendEvent( wxEVT_TEXT_ENTER, GetId() );
            sendEvent.SetEventObject( this );
            AddPendingEvent( sendEvent );
        }
    }
#ifdef __WXMSW__
    // Windows: Use native text handling for copy/paste/capitalize
    // Skip all other keys to let wxTextCtrl handle them natively
    // This fixes: copy (Ctrl+C), paste (Ctrl+V), select all (Ctrl+A), 
    // capitalization (Shift+letter), and all other native keyboard shortcuts
    else
    {
        aEvent.Skip();
    }
#else
    // Mac/Linux: Keep custom keyboard handling (works there)
    else if( keyCode < 256 && wxIsprint( keyCode ) )
    {
        int charToWrite = keyCode;

        if( charToWrite >= 'A' && charToWrite <= 'Z' && !aEvent.ShiftDown() )
        {
            charToWrite += 32; 
        }
        else if( aEvent.ShiftDown() )
        {
            switch( keyCode )
            {
                case '1': charToWrite = '!'; break;
                case '2': charToWrite = '@'; break;
                case '3': charToWrite = '#'; break;
                case '4': charToWrite = '$'; break;
                case '5': charToWrite = '%'; break;
                case '6': charToWrite = '^'; break;
                case '7': charToWrite = '&'; break;
                case '8': charToWrite = '*'; break;
                case '9': charToWrite = '('; break;
                case '0': charToWrite = ')'; break;
                case '-': charToWrite = '_'; break;
                case '=': charToWrite = '+'; break;
                case '[': charToWrite = '{'; break;
                case ']': charToWrite = '}'; break;
                case '\\': charToWrite = '|'; break;
                case ';': charToWrite = ':'; break;
                case '\'': charToWrite = '"'; break;
                case ',': charToWrite = '<'; break;
                case '.': charToWrite = '>'; break;
                case '/': charToWrite = '?'; break;
                case '`': charToWrite = '~'; break;
            }
        }

        m_textCtrl->WriteText( wxString( (char) charToWrite ) );
        aEvent.Skip( false );
    }
    else
    {
        aEvent.Skip();
    }
#endif
}


void STYLED_MULTILINE_TEXTCTRL::onTextChange( wxCommandEvent& aEvent )
{
#ifdef __WXMSW__
    // Windows: Adjust height immediately for smoother, less buggy appearance
    // Skip debouncing timer - Windows handles this better with immediate updates
    if( !m_isAdjusting )
    {
        adjustHeight();
    }
#else
    // Mac/Linux: Debounce height adjustment to prevent expensive layout calculations
    // This prevents expensive layout calculations on every keystroke
    if( m_adjustHeightTimer && !m_isAdjusting )
    {
        // Stop any pending timer
        if( m_adjustHeightTimer->IsRunning() )
        {
            m_adjustHeightTimer->Stop();
        }
        // Start new timer - will fire after user stops typing
        m_adjustHeightTimer->Start( ADJUST_HEIGHT_DELAY_MS, wxTIMER_ONE_SHOT );
    }
#endif
    aEvent.Skip();
}


void STYLED_MULTILINE_TEXTCTRL::onSize( wxSizeEvent& aEvent )
{
    if( m_textCtrl )
    {
        wxSize size = GetClientSize();
        int    textCtrlWidth = size.x - ( m_padding * 2 );
        int    textCtrlHeight = size.y - ( m_padding * 2 );
        
        if( textCtrlWidth < 0 )
            textCtrlWidth = 0;
        if( textCtrlHeight < 0 )
            textCtrlHeight = 0;
        
        m_textCtrl->SetSize( m_padding, m_padding, textCtrlWidth, textCtrlHeight );
        
        if( !m_isAdjusting )
        {
            m_isAdjusting = true;
            CallAfter(
                    [this]()
            { 
                adjustHeight(); 
                m_isAdjusting = false;
            } );
        }
    }
    aEvent.Skip();
}


void STYLED_MULTILINE_TEXTCTRL::adjustHeight()
{
    if( !m_textCtrl || m_isAdjusting )
        return;

    m_isAdjusting = true;
    int requiredHeight = calculateRequiredHeight();
    
    bool needsScrolling = false;
    if( requiredHeight < m_minHeight )
        requiredHeight = m_minHeight;
    if( requiredHeight > m_maxHeight )
    {
        requiredHeight = m_maxHeight;
        needsScrolling = true;
    }

    wxSize currentSize = GetSize();
    
    if( m_textCtrl )
    {
        long style = m_textCtrl->GetWindowStyle();
        if( needsScrolling )
        {
            style &= ~wxTE_NO_VSCROLL;
        }
        else
        {
            style |= wxTE_NO_VSCROLL;
        }
        m_textCtrl->SetWindowStyle( style );
    }
    
    if( currentSize.y != requiredHeight )
    {
        wxSize newSize( currentSize.x, requiredHeight );
        SetSize( newSize );
        SetMinSize( wxSize( -1, requiredHeight ) );
        
        if( m_textCtrl )
        {
            wxSize textCtrlSize = wxSize( newSize.x - ( m_padding * 2 ), requiredHeight - ( m_padding * 2 ) );
            if( textCtrlSize.x < 0 )
                textCtrlSize.x = 0;
            if( textCtrlSize.y < 0 )
                textCtrlSize.y = 0;
            m_textCtrl->SetSize( textCtrlSize );
        }
        
        wxWindow* parent = GetParent();
        if( parent )
        {
            if( parent->GetSizer() )
            {
                parent->GetSizer()->Layout();
            }
            parent->Refresh();
        }
        Refresh();
    }
    else if( m_textCtrl )
    {
        wxSize textCtrlSize = wxSize( currentSize.x - ( m_padding * 2 ), requiredHeight - ( m_padding * 2 ) );
        if( textCtrlSize.x < 0 )
            textCtrlSize.x = 0;
        if( textCtrlSize.y < 0 )
            textCtrlSize.y = 0;
        m_textCtrl->SetSize( textCtrlSize );
    }
    
    m_isAdjusting = false;
}


void STYLED_MULTILINE_TEXTCTRL::onAdjustHeightTimer( wxTimerEvent& aEvent )
{
    // Timer fired - user has stopped typing, now adjust height
    adjustHeight();
}


void STYLED_MULTILINE_TEXTCTRL::onKillFocus( wxFocusEvent& aEvent )
{
    // Flush any pending height adjustment when focus is lost
    if( m_adjustHeightTimer && m_adjustHeightTimer->IsRunning() )
    {
        m_adjustHeightTimer->Stop();
        adjustHeight();
    }
    aEvent.Skip();
}


int STYLED_MULTILINE_TEXTCTRL::calculateRequiredHeight() const
{
    if( !m_textCtrl )
        return m_minHeight;

    wxString text = m_textCtrl->GetValue();
    if( text.IsEmpty() )
        return m_minHeight;

    wxClientDC dc( const_cast<STYLED_MULTILINE_TEXTCTRL*>( this ) );
    dc.SetFont( m_textCtrl->GetFont() );
    
    wxSize textExtent = dc.GetTextExtent( wxT( "Ag" ) );
    int    lineHeight = textExtent.y;
    
    int panelWidth = GetSize().x;
    if( panelWidth <= 0 && GetParent() )
    {
        panelWidth = GetParent()->GetSize().x;
    }
    if( panelWidth <= 0 )
        panelWidth = 200;
    
    int availableWidth = panelWidth - ( m_padding * 2 ) - 4;
    if( availableWidth <= 0 )
        availableWidth = panelWidth - ( m_padding * 2 );
    if( availableWidth <= 0 )
        availableWidth = 200;
    
    int lineCount = m_textCtrl->GetNumberOfLines();
    int totalLineCount = 0;
    
    for( int i = 0; i < lineCount; i++ )
    {
        wxString line = m_textCtrl->GetLineText( i );
        if( line.IsEmpty() )
        {
            totalLineCount++;
        }
        else
        {
            wxSize textSize = dc.GetTextExtent( line );
            int    wrappedLines = 1;
            
            if( textSize.x > availableWidth && availableWidth > 0 )
            {
                wrappedLines = ( textSize.x + availableWidth - 1 ) / availableWidth;
                if( wrappedLines < 1 )
                    wrappedLines = 1;
            }
            
            totalLineCount += wrappedLines;
        }
    }
    
    if( totalLineCount == 0 )
        totalLineCount = 1;
    
    int calculatedHeight = ( totalLineCount * lineHeight ) + ( m_padding * 2 ) + 2;
    
    return calculatedHeight;
}


AI_CHAT_PANEL_BASE::AI_CHAT_PANEL_BASE( wxWindow* aParent, EDA_DRAW_FRAME* aFrame ) :
        wxPanel( aParent, wxID_ANY ),
        m_frame( aFrame ),
        m_tabContentContainer( nullptr ),
        m_tabContentSizer( nullptr ),
        m_inputBox( nullptr ),
        m_sendButton( nullptr ),
        m_signInButton( nullptr ),
        m_upgradeButton( nullptr ),
        m_quotaBanner( nullptr ),
        m_quotaBannerText( nullptr ),
        m_modeDropdown( nullptr ),
#ifdef TRACE_BACKEND_URL
        m_backendUrl( wxT( TRACE_BACKEND_URL ) ),  // Use build-time configured URL (includes /api/v1)
#else
        m_backendUrl( wxT( "http://localhost:8000/api/v1" ) ),  // Fallback
#endif
        m_requestInProgress( false ),
        m_sessionId( wxEmptyString ),
        m_conversationId( wxEmptyString ),
        m_streamingFlushTimer( nullptr ),
        m_aiEditInProgress( false ),
        m_aiEditStateCaptured( false ),
        m_lastSavedVersionId( wxEmptyString ),
        m_reloadDebounceTimer( nullptr ),
        m_reloadPending( false ),
        m_reloadInProgress( false ),
        m_streamingBatchTimer( nullptr ),
        m_batchUpdatePending( false ),
        m_panelAlive( std::make_shared<std::atomic<bool>>( true ) ),
        m_headerPanel( nullptr ),
        m_titleText( nullptr ),
        m_authButton( nullptr ),
        m_isDestroying( false ),
        m_streamingTabIndex( -1 ),
        m_tabBar( nullptr ),
        m_currentTabIndex( -1 )
{
    // Note: Backend clients are per-tab (created in createNewTab/loadPersistedTabs)

    // Create streaming flush timer
    m_streamingFlushTimer = new wxTimer( this, wxID_ANY );
    Bind( wxEVT_TIMER, &AI_CHAT_PANEL_BASE::onStreamingFlushTimer, this, m_streamingFlushTimer->GetId() );

    // Set up delete confirmation callback (will be applied to all tab tool executors)
    auto panelAlive = m_panelAlive;
    m_confirmationCallback = [this, panelAlive]( const std::string& filename ) -> std::future<bool>
        {
            auto promise = std::make_shared<std::promise<bool>>();
            std::future<bool> future = promise->get_future();

            wxTheApp->CallAfter( [this, panelAlive, filename, promise]()
            {
            if( !panelAlive->load() || m_isDestroying.load() || !m_frame )
                {
                    promise->set_value( false );
                    return;
                }

                wxString message = wxString::Format(
                    _( "Are you sure you want to delete the file '%s'?\n\n"
                       "This will also delete the corresponding KiCad file." ),
                    filename );
                
                wxMessageDialog dlg( m_frame, message, _( "Confirm File Deletion" ),
                                    wxYES_NO | wxICON_QUESTION | wxYES_DEFAULT );
                
                promise->set_value( dlg.ShowModal() == wxID_YES );
            } );

            return future;
    };

    // Try to restore session from keychain before building UI
    AUTH_MANAGER::Instance().TryRestoreSession();

    buildUI();

    // Listen for auth state changes to update UI (enable/disable controls)
    AUTH_MANAGER::Instance().Bind( EVT_AUTH_STATE_CHANGED, &AI_CHAT_PANEL_BASE::onAuthStateChanged, this );

    // Initialize local conversation database and load last conversation
    panelAlive = m_panelAlive;
    
    // Join any previous sync thread
    if( m_syncThread && m_syncThread->joinable() )
        m_syncThread->join();

    m_syncThread = std::make_unique<std::thread>( [panelAlive]() {
        CONVERSATION_DB::Instance().Initialize();
        
        // Note: loadLastConversation is now only called from the tab restoration logic
        // if loadPersistedTabs() returns false (no tabs to restore)
        
        // Silence unused variable warning
        (void)panelAlive;
    } );
}


AI_CHAT_PANEL_BASE::~AI_CHAT_PANEL_BASE()
{
    // Save open tabs state before destroying (for restoration on next launch)
    saveOpenTabs();
    
    // Stop and cleanup streaming flush timer
    if( m_streamingFlushTimer )
    {
        m_streamingFlushTimer->Stop();
        delete m_streamingFlushTimer;
        m_streamingFlushTimer = nullptr;
    }

    // Stop and cleanup reload debounce timer
    if( m_reloadDebounceTimer )
    {
        m_reloadDebounceTimer->Stop();
        delete m_reloadDebounceTimer;
        m_reloadDebounceTimer = nullptr;
    }

    // Stop and cleanup streaming batch timer
    if( m_streamingBatchTimer )
    {
        m_streamingBatchTimer->Stop();
        delete m_streamingBatchTimer;
        m_streamingBatchTimer = nullptr;
    }

    // Stop and cleanup per-tab idle status timers
    for( auto& tab : m_tabs )
    {
        if( tab.idleStatusTimer )
        {
            tab.idleStatusTimer->Stop();
            delete tab.idleStatusTimer;
            tab.idleStatusTimer = nullptr;
        }
    }

    // Unbind auth state change handler to prevent crashes
    AUTH_MANAGER::Instance().Unbind( EVT_AUTH_STATE_CHANGED, &AI_CHAT_PANEL_BASE::onAuthStateChanged, this );

    // CRITICAL: Set destruction flag FIRST to prevent CallAfter() callbacks from executing
    // This must happen before stopping clients to ensure callbacks see the flag
    m_isDestroying.store( true );

    // CRITICAL: Stop ALL per-tab backend clients and threads
    // Each tab has its own backend client for true parallel execution
    for( auto& tab : m_tabs )
    {
        tab.stopRequested.store( true );
        if( tab.backendClient )
            tab.backendClient->StopStream();
    }

    // Signal background threads that panel is being destroyed
    // Do this AFTER setting m_isDestroying and stopping clients to ensure proper shutdown order
    if( m_panelAlive )
        m_panelAlive->store( false );

    // Cleanup per-tab request threads (with timeout to prevent blocking)
    for( auto& tab : m_tabs )
    {
        if( tab.requestThread && tab.requestThread->joinable() )
            {
            // Give thread a brief chance to finish, then detach
            auto start = std::chrono::steady_clock::now();
            while( tab.isStreaming.load() &&
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start ).count() < 500 )
            {
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
        
            // Detach to prevent blocking - thread will see panelAlive=false and exit
            if( tab.requestThread->joinable() )
                tab.requestThread->detach();
        }
    }

    // Join sync thread with timeout
    if( m_syncThread && m_syncThread->joinable() )
    {
        auto start = std::chrono::steady_clock::now();
        constexpr int timeoutMs = 2000; // 2 seconds for DB operations
        
        while( m_syncThread->joinable() &&
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start ).count() < timeoutMs )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
        
        if( m_syncThread->joinable() )
            m_syncThread->join();
    }


    // Join conversation load thread with timeout
    if( m_conversationLoadThread && m_conversationLoadThread->joinable() )
    {
        auto start = std::chrono::steady_clock::now();
        constexpr int timeoutMs = 2000; // 2 seconds for DB operations
        
        while( m_conversationLoadThread->joinable() &&
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start ).count() < timeoutMs )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
        
        if( m_conversationLoadThread->joinable() )
            m_conversationLoadThread->join();
    }
}


bool AI_CHAT_PANEL_BASE::isAnyTabStreaming() const
{
    for( const auto& tab : m_tabs )
    {
        if( tab.isStreaming.load() )
            return true;
    }
    return false;
}


bool AI_CHAT_PANEL_BASE::claimFileOwnership( const wxString& aFilePath, int aTabIndex )
{
    std::lock_guard<std::mutex> lock( m_fileOwnershipMutex );
    
    // Soft ownership - just track which tab is working on the file
    // No blocking - the tool executor handles conflicts via file locks and hash checking
    auto it = m_fileOwnership.find( aFilePath );
    if( it != m_fileOwnership.end() && it->second != aTabIndex && it->second != -1 )
    {
        // Tab taking over file from another tab (soft ownership)
    }
    
    m_fileOwnership[aFilePath] = aTabIndex;
    return true;  // Always succeeds - soft ownership doesn't block
}


void AI_CHAT_PANEL_BASE::releaseFileOwnership( int aTabIndex )
{
    std::lock_guard<std::mutex> lock( m_fileOwnershipMutex );
    
    // Remove all file ownerships for this tab
    for( auto it = m_fileOwnership.begin(); it != m_fileOwnership.end(); )
    {
        if( it->second == aTabIndex )
        {
            it = m_fileOwnership.erase( it );
        }
        else
        {
            ++it;
        }
    }
}


int AI_CHAT_PANEL_BASE::getFileOwner( const wxString& aFilePath )
{
    std::lock_guard<std::mutex> lock( m_fileOwnershipMutex );
    
    auto it = m_fileOwnership.find( aFilePath );
    if( it != m_fileOwnership.end() )
        return it->second;
    
    return -1;  // No owner
}


void AI_CHAT_PANEL_BASE::markFileModifiedByTab( const wxString& aFilePath, int aTabIndex )
{
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;
    
    TAB_DATA& tab = m_tabs[aTabIndex];
    tab.fileModifiedDuringStream.store( true );
    tab.modifiedFiles.insert( aFilePath );
}


TAB_CONTENT_PANEL* AI_CHAT_PANEL_BASE::getCurrentContentPanel()
{
    return getContentPanel( m_currentTabIndex );
}


TAB_CONTENT_PANEL* AI_CHAT_PANEL_BASE::getContentPanel( int aTabIndex )
{
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return nullptr;
    
    return m_tabs[aTabIndex].contentPanel;
}


bool AI_CHAT_PANEL_BASE::isAnyTabStopRequested() const
{
    for( const auto& tab : m_tabs )
    {
        if( tab.stopRequested.load() )
            return true;
    }
    return false;
}


void AI_CHAT_PANEL_BASE::buildUI()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Header panel with title and auth button (hidden when signed in)
    m_headerPanel = new wxPanel( this, wxID_ANY );
    wxBoxSizer* headerSizer = new wxBoxSizer( wxHORIZONTAL );

    // Add Trace logo (the "t" icon) - use 24x24 size
    wxBitmap        logoBmp = KiBitmap( BITMAPS::icon_kicad_24 );
    wxStaticBitmap* logoBitmap = new wxStaticBitmap( m_headerPanel, wxID_ANY, logoBmp );
    headerSizer->Add( logoBitmap, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10 );

    m_titleText = new wxStaticText( m_headerPanel, wxID_ANY, wxT( "Trace AI" ) );
    wxFont titleFont = m_titleText->GetFont();
    titleFont.SetWeight( wxFONTWEIGHT_BOLD );
    titleFont.SetPointSize( titleFont.GetPointSize() + 2 );
    m_titleText->SetFont( titleFont );
    headerSizer->Add( m_titleText, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 8 );

    m_authButton = new wxButton( m_headerPanel, wxID_ANY, wxT( "Sign In" ) );
    m_authButton->SetMinSize( wxSize( 70, -1 ) );
    headerSizer->Add( m_authButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10 );

    m_headerPanel->SetSizer( headerSizer );
    mainSizer->Add( m_headerPanel, 0, wxEXPAND | wxTOP | wxBOTTOM, 8 );

    // Tab bar for multiple conversations
    m_tabBar = new CONVERSATION_TAB_BAR( this, wxID_ANY );
    mainSizer->Add( m_tabBar, 0, wxEXPAND | wxLEFT | wxRIGHT, 5 );

    // Separator line
    mainSizer->Add( new wxStaticLine( this ), 0, wxEXPAND | wxLEFT | wxRIGHT, 10 );

    // Create container for per-tab content panels (TRUE independence - each tab has its own chat history)
    m_tabContentContainer = new wxPanel( this, wxID_ANY );
    m_tabContentSizer = new wxBoxSizer( wxVERTICAL );
    m_tabContentContainer->SetSizer( m_tabContentSizer );
    m_tabContentContainer->SetMinSize( wxSize( 250, 300 ) );

    mainSizer->Add( m_tabContentContainer, 1, wxEXPAND | wxALL, 5 );

    // Control sizer for mode dropdown
    wxBoxSizer* controlSizer = new wxBoxSizer( wxHORIZONTAL );

    m_modeDropdown = new MODE_DROPDOWN_BUTTON( this, wxID_ANY );
    m_modeDropdown->SetInitialSize( m_modeDropdown->GetBestSize() );
    controlSizer->Add( m_modeDropdown, 0, wxALL, 5 );

    mainSizer->Add( controlSizer, 0, wxALIGN_LEFT );

    // Create quota/plan limit banner (hidden by default)
    // Contains text + upgrade button for clear call-to-action
    m_quotaBanner = new wxPanel( this, wxID_ANY );
    
    wxBoxSizer* bannerSizer = new wxBoxSizer( wxHORIZONTAL );
    
    m_quotaBannerText = new wxStaticText( m_quotaBanner, wxID_ANY, wxEmptyString, 
                                          wxDefaultPosition, wxDefaultSize, 
                                          wxST_ELLIPSIZE_END );  // Truncate with "..." if too long
    m_quotaBannerText->SetForegroundColour( wxColour( 107, 114, 128 ) );  // Gray text
    
    // Upgrade button inside the banner - clear call-to-action
    m_upgradeButton = new wxButton( m_quotaBanner, wxID_ANY, wxT( "Upgrade" ) );
    m_upgradeButton->SetMinSize( wxSize( 70, 24 ) );
    m_upgradeButton->SetToolTip( wxT( "Upgrade your plan" ) );
    m_upgradeButton->SetBackgroundColour( wxColour( 79, 70, 229 ) );  // Indigo/purple
    m_upgradeButton->SetForegroundColour( *wxWHITE );
    
    bannerSizer->AddSpacer( 10 );
    bannerSizer->Add( m_quotaBannerText, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    bannerSizer->Add( m_upgradeButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10 );
    
    m_quotaBanner->SetSizer( bannerSizer );
    m_quotaBanner->SetMinSize( wxSize( -1, 32 ) );
    m_quotaBanner->Hide();  // Hidden by default
    
    mainSizer->Add( m_quotaBanner, 0, wxEXPAND | wxLEFT | wxRIGHT, 5 );

    wxBoxSizer* inputSizer = new wxBoxSizer( wxHORIZONTAL );

    m_inputBox = new STYLED_MULTILINE_TEXTCTRL( this, wxID_ANY );

    m_sendButton = new wxBitmapButton( this, wxID_ANY, KiBitmapBundle( BITMAPS::icon_send, 24 ), wxDefaultPosition,
                                       wxDefaultSize, wxBORDER_NONE );

    // Create sign-in button for input area (shown when not authenticated)
    m_signInButton = new wxButton( this, wxID_ANY, wxT( "Sign In" ) );
    m_signInButton->SetMinSize( wxSize( 80, 32 ) );
    m_signInButton->SetToolTip( wxT( "Sign in to use Trace AI" ) );

    inputSizer->Add( m_inputBox, 1, wxALL | wxEXPAND, 5 );
    inputSizer->Add( m_sendButton, 0, wxALIGN_BOTTOM | wxBOTTOM | wxRIGHT, 5 );
    inputSizer->Add( m_signInButton, 0, wxALIGN_BOTTOM | wxBOTTOM | wxRIGHT, 5 );

    mainSizer->Add( inputSizer, 0, wxEXPAND );

    SetSizer( mainSizer );

    // Update auth button state
    updateAuthUI();

    Layout();

    m_sendButton->Bind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onSendMessage, this );
    m_inputBox->Bind( wxEVT_TEXT_ENTER, &AI_CHAT_PANEL_BASE::onSendMessage, this );
    m_signInButton->Bind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onAuthButtonClick, this );
    m_upgradeButton->Bind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onUpgradeButtonClick, this );
    // Banner is informational for trial/free users - no click-to-dismiss
    // It will only hide when user upgrades to paid plan
    m_modeDropdown->Bind( wxEVT_CHOICE, &AI_CHAT_PANEL_BASE::onModeChanged, this );
    m_authButton->Bind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onAuthButtonClick, this );

    // Tab bar events
    m_tabBar->Bind( wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGED, &AI_CHAT_PANEL_BASE::onTabSelected, this );
    Bind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onNewTab, this, ID_TAB_NEW );
    Bind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onHistorySelect, this, ID_TAB_HISTORY );

    // Bind close events for each possible tab
    for( int i = 0; i < CONVERSATION_TAB_BAR::MAX_TABS; i++ )
    {
        Bind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onTabClose, this, ID_TAB_CLOSE_BASE + i );
    }

    // Hand cursor on hover for auth button
    m_authButton->Bind( wxEVT_ENTER_WINDOW,
                        [this]( wxMouseEvent& )
                        {
                            m_authButton->SetCursor( wxCursor( wxCURSOR_HAND ) );
                        } );
    m_authButton->Bind( wxEVT_LEAVE_WINDOW,
                        [this]( wxMouseEvent& )
                        {
                            m_authButton->SetCursor( wxNullCursor );
                        } );

    // Hand cursor on hover for sign-in button in input area
    m_signInButton->Bind( wxEVT_ENTER_WINDOW,
                          [this]( wxMouseEvent& )
                          {
                              m_signInButton->SetCursor( wxCursor( wxCURSOR_HAND ) );
                          } );
    m_signInButton->Bind( wxEVT_LEAVE_WINDOW,
                          [this]( wxMouseEvent& )
                          {
                              m_signInButton->SetCursor( wxNullCursor );
                          } );

    Bind( wxEVT_SYS_COLOUR_CHANGED, &AI_CHAT_PANEL_BASE::onThemeChanged, this );

    // Create initial tab immediately (required for UI to display)
    int initialTab = createNewTab();
    if( initialTab >= 0 )
        switchToTab( initialTab );
    
    // Defer tab restoration to after derived class is fully constructed
    // (GetCurrentFileName() is pure virtual and not available during base class construction)
    CallAfter( [this]() {
        if( m_isDestroying.load() )
            return;
        
        // Try to restore persisted tabs from the previous session
        // If restoration succeeds, the initial empty tab gets replaced
        // If it fails (no persisted tabs), we keep the initial empty tab
        if( loadPersistedTabs() )
        {
            // Restored persisted tabs
        }
        else
        {
            // No persisted tabs - keep the initial empty tab (Cursor-style: simple and predictable)
            
            // If we ended up with no tabs (restoration cleared but found nothing),
            // create a fresh tab
            if( m_tabs.empty() )
            {
                int newTab = createNewTab();
                if( newTab >= 0 )
                    switchToTab( newTab );
            }
        }
    } );
}


void AI_CHAT_PANEL_BASE::onThemeChanged( wxSysColourChangedEvent& aEvent )
{
    // Ensure button bitmap is updated for the new theme
    // Check if current tab is streaming for correct button state
    bool isStreaming = false;
    if( m_currentTabIndex >= 0 && m_currentTabIndex < static_cast<int>( m_tabs.size() ) )
    {
        isStreaming = m_tabs[m_currentTabIndex].isStreaming.load();
    }
    updateButtonState( isStreaming );

    // Propagate event to children
    aEvent.Skip();

    Refresh();
}


void AI_CHAT_PANEL_BASE::updateAuthUI()
{
    if( !m_authButton || !m_titleText || !m_headerPanel )
        return;

    try
    {
        bool isAuthenticated = AUTH_MANAGER::Instance().IsAuthenticated();

        if( isAuthenticated )
        {
            // Hide the header panel when signed in - focus on tabs and chat
            // User can sign out from Account menu in main menu bar
            m_headerPanel->Hide();

            // Show send button, hide sign-in button
            if( m_sendButton )
                m_sendButton->Show();
            if( m_signInButton )
                m_signInButton->Hide();
            
            // Upgrade button is now inside the banner - no separate handling needed
            // It shows/hides with the banner automatically

            // Enable controls when authenticated - Ask mode is always available
            // Backend will enforce quota limits per mode
            if( m_inputBox )
                m_inputBox->Enable( true );
            if( m_sendButton )
                m_sendButton->Enable( true );
            if( m_modeDropdown )
                m_modeDropdown->Enable( true );
            
            // Fetch and show quota info banner on startup (trial time, usage)
            fetchAndShowQuotaInfo( true );  // true = startup, show banner once
        }
        else
        {
            // Show header with Sign In button when not signed in
            m_headerPanel->Show();
            m_titleText->SetLabel( wxT( "Trace AI" ) );
            m_authButton->SetLabel( wxT( "Sign In" ) );

            // Hide send button, show sign-in button in input area
            // Hide banner (and its upgrade button) when not authenticated
            if( m_sendButton )
                m_sendButton->Hide();
            if( m_signInButton )
                m_signInButton->Show();
            if( m_quotaBanner )
                m_quotaBanner->Hide();

            // Disable input box when not authenticated
            if( m_inputBox )
                m_inputBox->Enable( false );
            if( m_modeDropdown )
                m_modeDropdown->Enable( false );
        }

        Layout();
    }
    catch( ... )
    {
        // Auth manager might not be initialized yet - show sign-in state
        m_titleText->SetLabel( wxT( "Trace AI" ) );
        m_authButton->SetLabel( wxT( "Sign In" ) );

        if( m_sendButton )
            m_sendButton->Hide();
        if( m_signInButton )
            m_signInButton->Show();
        if( m_quotaBanner )
            m_quotaBanner->Hide();
        if( m_inputBox )
            m_inputBox->Enable( false );
        if( m_modeDropdown )
            m_modeDropdown->Enable( false );
    }
}


void AI_CHAT_PANEL_BASE::onAuthButtonClick( wxCommandEvent& aEvent )
{
    try
    {
        // This button is only visible when NOT signed in
        // When signed in, the header is hidden and user signs out from Account menu
        if( !AUTH_MANAGER::Instance().IsAuthenticated() )
        {
            AUTH_MANAGER::Instance().StartLogin();
        }

        // Update UI after auth action
        updateAuthUI();
    }
    catch( ... )
    {
        // Handle any auth manager errors gracefully
        TAB_CONTENT_PANEL* panel = getCurrentContentPanel();
        if( panel )
            panel->AddAIMessage( wxT( "Authentication error. Please try again." ) );
    }
}


void AI_CHAT_PANEL_BASE::onUpgradeButtonClick( wxCommandEvent& aEvent )
{
    // Open dashboard pricing page in default browser (requires login, triggers Stripe checkout)
    wxLaunchDefaultBrowser( wxT( "https://buildwithtrace.com/dashboard/pricing" ) );
}


void AI_CHAT_PANEL_BASE::showQuotaBanner( const wxString& aMessage, bool aShowUpgrade )
{
    if( !m_quotaBanner || !m_quotaBannerText )
        return;
    
    wxLogDebug( wxT( "[QUOTA_BANNER] Showing banner: %s (showUpgrade=%d)" ), aMessage, aShowUpgrade );
    
    // Set banner text
    m_quotaBannerText->SetLabel( aMessage );
    
    // Show/hide upgrade button based on context
    // - Show for quota exceeded, plan restricted errors
    // - Hide for informational banners (trial status, etc.)
    if( m_upgradeButton )
        m_upgradeButton->Show( aShowUpgrade );
    
    m_quotaBanner->Show();
    m_quotaBanner->Layout();  // Re-layout the banner to adjust for button visibility
    Layout();
    Refresh();
}


void AI_CHAT_PANEL_BASE::hideQuotaBanner()
{
    if( m_quotaBanner && m_quotaBanner->IsShown() )
    {
        wxLogDebug( wxT( "[QUOTA_BANNER] Hiding banner (was visible)" ) );
        m_quotaBanner->Hide();
        Layout();
    }
}


void AI_CHAT_PANEL_BASE::fetchAndShowQuotaInfo( bool aIsStartup )
{
    // Only fetch if authenticated
    if( !AUTH_MANAGER::Instance().IsAuthenticated() )
    {
        wxLogDebug( wxT( "[QUOTA_BANNER] Not authenticated, skipping quota fetch" ) );
        return;
    }

    // Get auth token
    std::string authToken = AUTH_MANAGER::Instance().GetAuthToken().ToStdString();
    if( authToken.empty() )
    {
        wxLogDebug( wxT( "[QUOTA_BANNER] No auth token, skipping quota fetch" ) );
        return;
    }

    wxLogDebug( wxT( "[QUOTA_BANNER] Fetching quota info (isStartup=%d)..." ), aIsStartup );

    // Capture backend URL and startup flag for thread
    std::string backendUrl = m_backendUrl.ToStdString();
    bool isStartup = aIsStartup;

    // Fetch quota in background thread to avoid blocking UI
    std::thread( [this, authToken, backendUrl, isStartup]() {
        try
        {
            // Create client and fetch quota
            AI_BACKEND_CLIENT client( backendUrl );
            USER_QUOTA_INFO quota = client.GetUserQuota( authToken );

            wxLogDebug( wxT( "[QUOTA_BANNER] API response: success=%d, plan=%s, code=%s, dailyCostUsed=%.4f, dailyCostCap=%.2f, isTrial=%d, trialHoursLeft=%d, creditsRemaining=%d" ),
                quota.success, wxString::FromUTF8( quota.plan.c_str() ), 
                wxString::FromUTF8( quota.code.c_str() ), quota.dailyCostUsed, quota.dailyCostCap,
                quota.isTrial, quota.trialHoursLeft, quota.creditsRemaining );

            if( !quota.success )
            {
                wxLogDebug( wxT( "[QUOTA_BANNER] Quota fetch failed, not showing banner" ) );
                return;
            }

            // Check if this is an on-demand (credit-based) plan
            bool isOnDemand = ( quota.plan.find( "on_demand" ) != std::string::npos );
            
            // Determine if quota is LOW (warning threshold) - now cost-based
            bool isLowQuota = false;
            
            if( quota.plan == "free" )
            {
                isLowQuota = true;  // Free users always see banner
            }
            else if( quota.isTrial )
            {
                // Trial: check cost usage and time remaining
                bool lowTime = ( quota.trialHoursLeft >= 0 && quota.trialHoursLeft <= QuotaConfig::TRIAL_LOW_HOURS_THRESHOLD );
                bool highUsage = false;
                if( quota.dailyCostCap > 0 )
                {
                    double usagePercent = ( quota.dailyCostUsed / quota.dailyCostCap ) * 100.0;
                    highUsage = ( usagePercent >= QuotaConfig::DAILY_USAGE_WARNING_PERCENT );
                }
                isLowQuota = lowTime || highUsage;
            }
            else if( isOnDemand )
            {
                isLowQuota = ( quota.creditsRemaining >= 0 && quota.creditsRemaining <= QuotaConfig::CREDITS_WARNING_THRESHOLD );
            }
            else if( quota.code == "SUBSCRIPTION_PAST_DUE" || quota.code == "SUBSCRIPTION_CANCELLED" )
            {
                isLowQuota = true;  // Always show warning for subscription issues
            }
            else if( quota.code == "DAILY_COST_LIMIT_REACHED" || quota.code == "MONTHLY_COST_LIMIT_REACHED" )
            {
                isLowQuota = true;  // Always show when cost limit reached
            }
            
            // Decide whether to show banner:
            // 1. On startup: always show (will be hidden after first message)
            // 2. After message: only show if quota is LOW (warning)
            // 3. Subscription plans (pro_*, ultra_*, internal): never show UNLESS past_due/cancelled/limit_reached
            
            bool isSubscriptionPlan = ( quota.plan != "trial" && quota.plan != "free" && !isOnDemand );
            bool hasSubscriptionIssue = ( quota.code == "SUBSCRIPTION_PAST_DUE" || quota.code == "SUBSCRIPTION_CANCELLED" 
                                          || quota.code == "DAILY_COST_LIMIT_REACHED" || quota.code == "MONTHLY_COST_LIMIT_REACHED" );
            
            if( isSubscriptionPlan && !hasSubscriptionIssue )
            {
                wxLogDebug( wxT( "[QUOTA_BANNER] Skipping banner for healthy subscription plan: %s" ), wxString::FromUTF8( quota.plan.c_str() ) );
                return;
            }
            
            // If not startup and not low quota, hide the banner (user sent a message)
            if( !isStartup && !isLowQuota )
            {
                wxLogDebug( wxT( "[QUOTA_BANNER] Not startup and quota not low, hiding banner" ) );
                wxTheApp->CallAfter( [this]() {
                    hideQuotaBanner();
                } );
                return;
            }

            // Build banner message based on quota info - NOW COST-BASED
            wxString bannerMsg;
            bool isWarning = false;
            
            if( isOnDemand )
            {
                // On-demand (credit-based) user: show credits remaining
                if( quota.creditsRemaining >= 0 )
                {
                    if( quota.creditsRemaining <= QuotaConfig::CREDITS_CRITICAL_THRESHOLD )
                    {
                        bannerMsg = wxString::Format( 
                            wxT( "Low credits! Only %d remaining" ),
                            quota.creditsRemaining
                        );
                        isWarning = true;
                    }
                    else
                    {
                        bannerMsg = wxString::Format( 
                            wxT( "%d credits remaining" ),
                            quota.creditsRemaining
                        );
                    }
                }
                else
                {
                    // Credits not available from backend yet - show plan name
                    bannerMsg = wxT( "Credit-based plan active" );
                }
            }
            else if( quota.isTrial && quota.trialHoursLeft >= 0 )
            {
                // Trial user: show time remaining and COST usage (not requests)
                double cap = quota.dailyCostCap > 0 ? quota.dailyCostCap : 0.50;  // Default $0.50/day
                double usagePercent = cap > 0 ? ( quota.dailyCostUsed / cap ) * 100.0 : 0;
                
                if( quota.trialHoursLeft <= QuotaConfig::TRIAL_LOW_HOURS_THRESHOLD )
                {
                    bannerMsg = wxString::Format( 
                        wxT( "Trial ending soon! %dh left | $%.2f/$%.2f used today" ),
                        quota.trialHoursLeft,
                        quota.dailyCostUsed,
                        cap
                    );
                    isWarning = true;
                }
                else if( usagePercent >= QuotaConfig::DAILY_USAGE_WARNING_PERCENT )
                {
                    bannerMsg = wxString::Format( 
                        wxT( "Daily limit almost reached! $%.2f/$%.2f | %dh trial left" ),
                        quota.dailyCostUsed,
                        cap,
                        quota.trialHoursLeft
                    );
                    isWarning = true;
                }
                else
                {
                    bannerMsg = wxString::Format( 
                        wxT( "Trial: %dh left | $%.2f/$%.2f today" ),
                        quota.trialHoursLeft,
                        quota.dailyCostUsed,
                        cap
                    );
                }
            }
            else if( quota.code == "TRIAL_ACTIVE" )
            {
                // Trial active but couldn't parse hours - show generic message with cost
                double cap = quota.dailyCostCap > 0 ? quota.dailyCostCap : 0.50;
                bannerMsg = wxString::Format( 
                    wxT( "Trial active | $%.2f/$%.2f today" ),
                    quota.dailyCostUsed,
                    cap
                );
            }
            else if( quota.code == "DAILY_COST_LIMIT_REACHED" )
            {
                // Daily cost limit reached
                bannerMsg = wxString::Format( 
                    wxT( "Daily limit reached ($%.2f). Try again tomorrow or upgrade." ),
                    quota.dailyCostCap > 0 ? quota.dailyCostCap : quota.dailyCostUsed
                );
                isWarning = true;
            }
            else if( quota.code == "MONTHLY_COST_LIMIT_REACHED" )
            {
                // Monthly cost limit reached
                bannerMsg = wxString::Format( 
                    wxT( "Monthly limit reached ($%.2f). Upgrade for more usage." ),
                    quota.monthlyCostCap > 0 ? quota.monthlyCostCap : quota.monthlyCostUsed
                );
                isWarning = true;
            }
            else if( quota.plan == "free" )
            {
                // Free user after trial - always show
                bannerMsg = wxT( "Free plan - Ask mode only. Upgrade for Agent & Plan modes." );
                isWarning = true;
            }
            else if( quota.code == "SUBSCRIPTION_PAST_DUE" )
            {
                // Payment failed - show warning but allow access (grace period)
                bannerMsg = wxT( "Payment failed. Please update your payment method to avoid service interruption." );
                isWarning = true;
            }
            else if( quota.code == "SUBSCRIPTION_CANCELLED" )
            {
                // Subscription cancelled - show warning
                bannerMsg = wxT( "Your subscription has been cancelled. Please renew to continue." );
                isWarning = true;
            }

            if( !bannerMsg.IsEmpty() )
            {
                wxLogDebug( wxT( "[QUOTA_BANNER] Scheduling banner display: %s (isWarning=%d)" ), 
                            bannerMsg, isWarning );
                // Show banner on main thread
                wxTheApp->CallAfter( [this, bannerMsg, isWarning]() {
                    showQuotaBanner( bannerMsg, isWarning );
                } );
            }
            else
            {
                wxLogDebug( wxT( "[QUOTA_BANNER] No banner message to show (empty)" ) );
            }
        }
        catch( const std::exception& e )
        {
            // Silently fail - quota display is informational
        }
    } ).detach();
}


void AI_CHAT_PANEL_BASE::onAuthStateChanged( wxCommandEvent& aEvent )
{
    // Auth state changed (e.g., user signed in via browser callback)
    // Update UI to reflect the new auth state
    updateAuthUI();
    Layout();
    Refresh();

    // Start/stop conversation sync based on auth state
    if( AUTH_MANAGER::Instance().IsAuthenticated() )
    {
        // User signed in - start sync and fetch from Supabase
        AUTH_USER user = AUTH_MANAGER::Instance().GetCurrentUser();
        
        // Update local conversations with user_id
        CONVERSATION_DB& db = CONVERSATION_DB::Instance();
        db.SetUserIdForLocalConversations( user.id );
        
        // Start background sync
        CONVERSATION_SYNC::Instance().Start();
        
        // Fetch conversations from Supabase in background
        // Join previous fetch thread if exists
        if( m_syncThread && m_syncThread->joinable() )
            m_syncThread->join();

        m_syncThread = std::make_unique<std::thread>( []() {
            CONVERSATION_SYNC::Instance().FetchFromSupabase();
        } );
    }
    else
    {
        // User signed out - stop sync but keep local data
        CONVERSATION_SYNC::Instance().Stop();
    }
}


void AI_CHAT_PANEL_BASE::onModeChanged( wxCommandEvent& aEvent )
{
    // Mode updated in dropdown - no additional action needed
    // The mode will be read when sending messages
}


void AI_CHAT_PANEL_BASE::onSendMessage( wxCommandEvent& aEvent )
{
    wxString message = m_inputBox->GetValue();
    message.Trim();

    if( message.IsEmpty() )
        return;

    // Update cached project path (safe access for destructor - avoids pure virtual call)
    m_cachedProjectPath = GetCurrentFileName();

    // Check if the CURRENT tab is streaming (per-tab check for parallel execution)
    if( m_currentTabIndex >= 0 && m_currentTabIndex < static_cast<int>( m_tabs.size() ) )
    {
        if( m_tabs[m_currentTabIndex].isStreaming.load() )
        {
            // This tab is already streaming - the send button should be a stop button
            // so this shouldn't normally happen, but guard against it
        return;
        }
    }

    // PROACTIVE TOKEN REFRESH: Check and refresh token BEFORE making any request
    // This prevents "Session expired" errors by ensuring we always have a valid token
    if( AUTH_MANAGER::Instance().IsAuthenticated() )
    {
        if( AUTH_MANAGER::Instance().IsTokenExpiringSoon() )
        {
            if( AUTH_MANAGER::Instance().RefreshAccessToken() )
            {
                // Proactive token refresh successful
            }
            else
            {
                // Proactive token refresh failed, but continuing with existing token
                // Don't block the request - the backend might still accept the token
                // If it truly fails, we'll get auth_error from the backend
            }
        }
    }

    // Create conversation in local DB if this is the first message
    if( m_conversationId.IsEmpty() && m_currentTabIndex >= 0 )
    {
        CONVERSATION_DB& db = CONVERSATION_DB::Instance();
        
        // Ensure DB is initialized (in case async init hasn't completed yet)
        if( !db.IsOpen() )
        {
            if( !db.Initialize() )
            {
                wxLogError( wxT( "Failed to initialize conversation database" ) );
        return;
    }
        }
        
        wxString userId = wxEmptyString;
        if( AUTH_MANAGER::Instance().IsAuthenticated() )
        {
            AUTH_USER user = AUTH_MANAGER::Instance().GetCurrentUser();
            userId = user.id;
        }
        
        auto conv = db.CreateConversation( userId, GetCurrentFileName(), m_sessionId );
        if( conv.has_value() )
        {
            m_conversationId = conv->id;
            
            // Update tab data
            if( m_currentTabIndex < static_cast<int>( m_tabs.size() ) )
            {
                m_tabs[m_currentTabIndex].conversationId = m_conversationId;
                
                // Save tab state now that we have a valid conversation ID
                saveOpenTabs();
            }
            }
            else
            {
            wxLogError( wxT( "Failed to create conversation in database" ) );
            return;
        }
    }

    // Save user message to local DB (only if we have a valid conversation)
    if( !m_conversationId.IsEmpty() )
    {
        CONVERSATION_DB& db = CONVERSATION_DB::Instance();
        
        auto msg = db.SaveMessage( m_conversationId, wxT( "user" ), message );
        if( !msg.has_value() )
        {
            wxLogError( wxT( "Failed to save message to database for conversation: %s" ), m_conversationId );
        }
        
        // Update conversation title from first message if not set
        if( m_currentTabIndex >= 0 && m_currentTabIndex < static_cast<int>( m_tabs.size() ) )
        {
            if( m_tabs[m_currentTabIndex].title == wxT( "New Chat" ) )
            {
                wxString title = message.Left( 30 );
                if( message.length() > 30 )
                    title += wxT( "..." );
                
                m_tabs[m_currentTabIndex].title = title;
                m_tabBar->SetTabTitle( m_currentTabIndex, title );
                
                db.UpdateConversationTitle( m_conversationId, title );
            }
        }
    }

    // Add user message to the current tab's content panel (TRUE independence)
    TAB_CONTENT_PANEL* contentPanel = getCurrentContentPanel();
    if( contentPanel )
        contentPanel->AddUserMessage( message );
    
    m_inputBox->Clear();
    
    // Clear the draft for this tab since message was sent
    if( m_currentTabIndex >= 0 && m_currentTabIndex < static_cast<int>( m_tabs.size() ) )
        {
        m_tabs[m_currentTabIndex].draftInput.Clear();
    }
    
    sendToBackendAsync( message );
}


// Note: SendJsonRequest has been removed. Direct backend communication is now
// handled by AI_BACKEND_CLIENT via sendToBackendAsync().


// NOTE: HandleBackendEvent was removed - dead code
// All event handling now goes through handleBackendEventDirect with explicit tab index


void AI_CHAT_PANEL_BASE::SetDrcCallback( std::function<nlohmann::json()> aCallback )
        {
    // Store callback to set on all current AND future tabs
    m_drcCallback = aCallback;
    
    // Set on all existing tabs' tool executors
    for( auto& tab : m_tabs )
        {
        if( tab.toolExecutor )
            tab.toolExecutor->SetDrcCallback( aCallback );
    }
}


void AI_CHAT_PANEL_BASE::SetErcCallback( std::function<nlohmann::json()> aCallback )
    {
    // Store callback to set on all current AND future tabs
    m_ercCallback = aCallback;
    
    // Set on all existing tabs' tool executors
    for( auto& tab : m_tabs )
    {
        if( tab.toolExecutor )
            tab.toolExecutor->SetErcCallback( aCallback );
        }
    }


void AI_CHAT_PANEL_BASE::SetAnnotateCallback( std::function<nlohmann::json( const nlohmann::json& )> aCallback )
{
    // Store callback to set on all current AND future tabs
    m_annotateCallback = aCallback;
    
    // Set on all existing tabs' tool executors
    for( auto& tab : m_tabs )
    {
        if( tab.toolExecutor )
            tab.toolExecutor->SetAnnotateCallback( aCallback );
            }
        }

void AI_CHAT_PANEL_BASE::SetGerberCallback( std::function<nlohmann::json( const nlohmann::json& )> aCallback )
{
    // Store callback to set on all current AND future tabs
    m_gerberCallback = aCallback;
    
    // Set on all existing tabs' tool executors
    for( auto& tab : m_tabs )
    {
        if( tab.toolExecutor )
            tab.toolExecutor->SetGerberCallback( aCallback );
    }
}

void AI_CHAT_PANEL_BASE::SetDrillCallback( std::function<nlohmann::json( const nlohmann::json& )> aCallback )
{
    // Store callback to set on all current AND future tabs
    m_drillCallback = aCallback;
    
    // Set on all existing tabs' tool executors
    for( auto& tab : m_tabs )
    {
        if( tab.toolExecutor )
            tab.toolExecutor->SetDrillCallback( aCallback );
    }
}

void AI_CHAT_PANEL_BASE::SetAutorouteCallback( std::function<nlohmann::json( const nlohmann::json& )> aCallback )
{
    // Store callback to set on all current AND future tabs
    m_autorouteCallback = aCallback;
    
    // Set on all existing tabs' tool executors
    for( auto& tab : m_tabs )
    {
        if( tab.toolExecutor )
            tab.toolExecutor->SetAutorouteCallback( aCallback );
    }
}

void AI_CHAT_PANEL_BASE::SetSnapshotCallback( std::function<std::string()> aCallback )
    {
    // Store callback to set on all current AND future tabs
    m_snapshotCallback = aCallback;
            
    // Set on all existing tabs' tool executors
    for( auto& tab : m_tabs )
            {
        if( tab.toolExecutor )
            tab.toolExecutor->SetSnapshotCallback( aCallback );
            }
}


void AI_CHAT_PANEL_BASE::configureToolExecutor( AI_TOOL_EXECUTOR* aToolExecutor )
{
    if( !aToolExecutor )
        return;
    
    // Note: App type is set in sendToBackendAsync() since GetAppType() is pure virtual
    // and not available during base class construction
    
    // Apply all stored callbacks
    if( m_drcCallback )
        aToolExecutor->SetDrcCallback( m_drcCallback );
    if( m_ercCallback )
        aToolExecutor->SetErcCallback( m_ercCallback );
    if( m_annotateCallback )
        aToolExecutor->SetAnnotateCallback( m_annotateCallback );
    if( m_gerberCallback )
        aToolExecutor->SetGerberCallback( m_gerberCallback );
    if( m_drillCallback )
        aToolExecutor->SetDrillCallback( m_drillCallback );
    if( m_autorouteCallback )
        aToolExecutor->SetAutorouteCallback( m_autorouteCallback );
    if( m_snapshotCallback )
        aToolExecutor->SetSnapshotCallback( m_snapshotCallback );
    if( m_confirmationCallback )
        aToolExecutor->SetConfirmationCallback( m_confirmationCallback );
}


void AI_CHAT_PANEL_BASE::handleBackendEventDirect( const AI_BACKEND_EVENT& aEvent, int aTabIndex )
{
    // Critical: Don't process events if panel is being destroyed
    if( m_isDestroying.load() )
        return;
    
    // Validate tab index for parallel streaming safety
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;
    
    // Reset idle status timer - any event means backend is responsive
    // This restarts the 2-second countdown before showing "Working..."
    resetIdleStatusTimer( aTabIndex );
    
    // Flush any buffered text before processing non-text events
    // This ensures correct ordering (text appears before status/done messages)
    if( aEvent.type != AI_EVENT_TYPE::TEXT_DELTA )
    {
        flushStreamingBuffer( aTabIndex );
    }

    switch( aEvent.type )
    {
    case AI_EVENT_TYPE::TEXT_DELTA:
    {
        if( !aEvent.content.empty() )
        {
            // Buffer the text instead of immediate update (pass tab index for parallel safety)
            bufferStreamingText( wxString::FromUTF8( aEvent.content ), aTabIndex );
        }
        break;
    }
    case AI_EVENT_TYPE::STATUS:
    {
        onStreamingStatus( wxString::FromUTF8( aEvent.content ), aTabIndex );
        break;
    }
    case AI_EVENT_TYPE::TITLE_UPDATE:
    {
        wxString newTitle = wxString::FromUTF8( aEvent.content );
        if( !newTitle.IsEmpty() )
        {
            // Update the specific tab, not a global streaming tab
            if( m_tabBar && aTabIndex >= 0 && aTabIndex < static_cast<int>( m_tabs.size() ) )
            {
                m_tabs[aTabIndex].title = newTitle;
                m_tabBar->SetTabTitle( aTabIndex, newTitle );
                
                // Also save to DB using the tab's conversation ID
                wxString convId = m_tabs[aTabIndex].conversationId;
                if( !convId.IsEmpty() )
                {
                    CONVERSATION_DB& db = CONVERSATION_DB::Instance();
                    db.UpdateConversationTitle( convId, newTitle );
                }
            }
        }
        break;
    }
    case AI_EVENT_TYPE::MODE_TRANSITION:
    {
        // Mode transition events are informational - no special handling needed
        // The backend handles mode transitions internally
        break;
    }
    case AI_EVENT_TYPE::PHASE_UPDATE:
    {
        // Phase update events are informational - no special handling needed
        // The backend handles phase updates internally
        break;
    }
    case AI_EVENT_TYPE::TOOL_CALL:
    {
        // Capture state before file-modifying tools (search_replace, write)
        if( ( aEvent.toolName == "search_replace" || aEvent.toolName == "write" ) 
            && m_aiEditInProgress && !m_aiEditStateCaptured )
        {
            wxString filePath = GetCurrentFileName();
            if( m_frame && !filePath.IsEmpty() )
            {
                CaptureStateForAIEdit( filePath );
                m_aiEditStateCaptured = true;
            }
        }
        break;
    }
    case AI_EVENT_TYPE::FILE_EDIT:
    {
        // Handle file edit with optional incremental diff support
        // Pass tab index for per-tab file modification tracking
        HandleFileEditEvent( aEvent, aTabIndex );
        break;
    }
    case AI_EVENT_TYPE::PROGRESS:
    {
        // Update the specific tab's panel directly (TRUE independence)
        TAB_CONTENT_PANEL* panel = getContentPanel( aTabIndex );
        if( !panel )
            break;
            
        if( aEvent.data.contains( "expandable" ) && aEvent.data["expandable"].get<bool>() )
        {
            wxString summary = wxString::FromUTF8( aEvent.data.value( "summary", "View more" ) );
            std::vector<wxString> items;
            if( aEvent.data.contains( "items" ) && aEvent.data["items"].is_array() )
            {
                for( const auto& item : aEvent.data["items"] )
                    items.push_back( wxString::FromUTF8( item.get<std::string>() ) );
            }
            if( panel->GetChatHistory() )
                panel->GetChatHistory()->AddExpandableSection( summary, items );
        }
        break;
    }
    case AI_EVENT_TYPE::EVENT_ERROR:
    {
        onBackendResponse( wxString::FromUTF8( aEvent.error ), false, aTabIndex );
        break;
    }
    case AI_EVENT_TYPE::AUTH_ERROR:
    {
        AUTH_MANAGER::Instance().SignOut();
        updateAuthUI();
        onBackendResponse( wxT( "Session expired. Please sign in again." ), false, aTabIndex, false );
        break;
    }
    case AI_EVENT_TYPE::QUOTA_EXCEEDED:
    {
        // Show upgrade message for quota exceeded (402)
        wxString upgradeMsg = wxT( "⚠️ **Plan Limit Reached**\n\n" );
        upgradeMsg += wxT( "You've used all your AI requests for this billing period.\n\n" );
        upgradeMsg += wxT( "**[Upgrade your plan](https://buildwithtrace.com/dashboard/pricing)** to continue using Trace AI." );
        onBackendResponse( upgradeMsg, false, aTabIndex, false );
        break;
    }
    case AI_EVENT_TYPE::PLAN_RESTRICTED:
    {
        // Show upgrade message for plan-restricted features (403)
        wxString upgradeMsg = wxT( "⚠️ **Paid Plan Required**\n\n" );
        upgradeMsg += wxT( "You're on the **Free plan**. Trace AI requires a paid subscription.\n\n" );
        upgradeMsg += wxT( "**[Upgrade to Pro](https://buildwithtrace.com/dashboard/pricing)** to unlock:\n" );
        upgradeMsg += wxT( "• AI-powered schematic design\n" );
        upgradeMsg += wxT( "• Automated PCB layout\n" );
        upgradeMsg += wxT( "• Component selection assistance\n" );
        upgradeMsg += wxT( "• And much more..." );
        onBackendResponse( upgradeMsg, false, aTabIndex, false );
        break;
    }
    case AI_EVENT_TYPE::DONE:
    case AI_EVENT_TYPE::VERSIONS_LIST:
    case AI_EVENT_TYPE::VERSION_SAVED:
    case AI_EVENT_TYPE::VERSION_RESTORED:
        // These are handled in the completion callback
        break;
    }
}


void AI_CHAT_PANEL_BASE::sendToBackendAsync( const wxString& aMessage )
{
    // Per-tab streaming check for parallel execution
    int tabIndex = m_currentTabIndex;
    if( tabIndex < 0 || tabIndex >= static_cast<int>( m_tabs.size() ) )
    {
        wxLogError( wxT( "Invalid tab index for sending message" ) );
        return;
    }
    
    TAB_DATA& tab = m_tabs[tabIndex];
    
    // Check if THIS tab is already streaming (per-tab check for true parallelism)
    bool wasStreaming = tab.isStreaming.exchange( true );
    if( wasStreaming )
    {
        return;
    }

    // Reset per-tab streaming state
    tab.stopRequested.store( false );
    tab.pendingStreamingResponse.Clear();
    m_streamingTabIndex = tabIndex;  // Track which tab started this stream

    // Reset streaming buffer state for new request (per-tab)
    flushStreamingBuffer( tabIndex );  // Flush any remaining buffered content from previous request
    tab.streamingBuffer.Clear();
    tab.pendingDeltaCount = 0;
    tab.isFirstStreamingFlush = true;  // Next flush will be the first of this new stream
    if( m_streamingFlushTimer && m_streamingFlushTimer->IsRunning() )
    {
        m_streamingFlushTimer->Stop();
    }

    m_aiEditInProgress = true;
    m_aiEditStateCaptured = false;
    m_batchUpdatePending.store( false );        // Reset batch update flag for new stream
    
    // Reset per-tab file modification tracking for this stream
    if( m_currentTabIndex >= 0 && m_currentTabIndex < static_cast<int>( m_tabs.size() ) )
    {
        m_tabs[m_currentTabIndex].fileModifiedDuringStream.store( false );
        m_tabs[m_currentTabIndex].modifiedFiles.clear();
    }
    
    // Reset conversion tracking for this tab's new session
    if( tab.toolExecutor )
    {
        tab.toolExecutor->ResetConversionState();
    }
    
    // Track which file this tab is working on (for per-tab state management)
    // NOTE: We DON'T block if another tab is editing - instead we let the tool executor
    // handle conflicts gracefully using file hashing and fuzzy matching.
    // This gives Cursor-like seamless concurrent editing where:
    // - Edits are applied in order (via file locks and conversion queue)
    // - File changes are detected via hash comparison
    // - The AI can retry if content doesn't match (fuzzy matching helps)
    wxString currentFile = GetCurrentFileName();
    if( !currentFile.IsEmpty() )
    {
        // Soft ownership - just tracks which tab is working on what, no blocking
        claimFileOwnership( currentFile, m_currentTabIndex );
    }

    // Switch Send button to Stop button while AI is responding
    updateButtonState( true );
    m_inputBox->Enable(); // Allow typing while AI responds (but can't send)

    // Show typing indicator in the current tab's panel
    TAB_CONTENT_PANEL* currentPanel = getCurrentContentPanel();
    if( currentPanel )
        currentPanel->ShowTypingIndicator();

    // Start idle status timer - will show "Working..." if no events for 2 seconds
    resetIdleStatusTimer( tabIndex );

    // Generate session ID if not set (GLOBAL - shared by all tabs in this app session)
    if( m_sessionId.IsEmpty() )
    {
        m_sessionId = CONVERSATION_DB::GenerateUUID();
    }

    // Proactively refresh token if expiring soon
    if( AUTH_MANAGER::Instance().IsAuthenticated() )
    {
        if( AUTH_MANAGER::Instance().IsTokenExpiringSoon() )
        {
            if( !AUTH_MANAGER::Instance().RefreshAccessToken() )
            {
                // Proactive token refresh failed - will sign out on 401
            }
        }
    }

    // Ensure schematic is saved before AI can read it
    wxString filePath = EnsureFileSavedForAI();
    
    // Get trace file path if kicad file path provided
    wxString traceFilePath = filePath;
    wxString kicadFilePath = filePath;
    if( !filePath.IsEmpty() )
    {
        traceFilePath = ConvertToTraceFile( filePath );
    }

    // Get auth tokens
    wxString authToken = AUTH_MANAGER::Instance().GetAuthToken();
    wxString refreshToken = AUTH_MANAGER::Instance().GetRefreshToken();

    // Create per-tab backend client AND tool executor for TRUE parallel execution
    // Each tab gets its own HTTP client and tool executor so streams don't block each other
    if( !tab.backendClient )
    {
        tab.backendClient = std::make_unique<AI_BACKEND_CLIENT>( m_backendUrl.ToStdString() );
    }
    
    // Ensure tab has its own tool executor (prevents file access deadlocks between tabs!)
    if( !tab.toolExecutor )
    {
        tab.toolExecutor = std::make_unique<AI_TOOL_EXECUTOR>();
        configureToolExecutor( tab.toolExecutor.get() );  // Apply all callbacks
        tab.backendClient->SetToolExecutor( tab.toolExecutor.get() );
    }
    else
    {
        // Update app type in case it changed
        tab.toolExecutor->SetAppType( GetAppType().ToStdString() );
    }
    
    // Set allowed directories based on current project (security sandbox)
    if( !currentFile.IsEmpty() && tab.toolExecutor )
    {
        wxFileName fn( currentFile );
        wxString projectDir = fn.GetPath();
        if( !projectDir.IsEmpty() )
        {
            tab.toolExecutor->ClearAllowedProjectDirs();
            tab.toolExecutor->AddAllowedProjectDir( projectDir.ToStdString() );
        }
    }
    
    AI_BACKEND_CLIENT* tabClient = tab.backendClient.get();

    // Set up event callback for streaming (uses per-tab client)
    // NOTE: Use conversationId instead of tabIndex to avoid stale index issues during tab closure/shutdown
    auto panelAlive = m_panelAlive;
    wxString convId = tab.conversationId;
    tabClient->SetEventCallback( [this, panelAlive, convId]( const AI_BACKEND_EVENT& aEvent )
    {
        if( !panelAlive->load() )
            return;

        safeCallAfter( [this, convId, aEvent]()
        {
            // Find tab by conversation ID (indices may have shifted if tabs were closed)
            TAB_DATA* tab = findTabByConversationId( convId );
            if( !tab )
                return;  // Tab was closed while streaming
            
            // Check if this tab's stream was stopped
            if( tab->stopRequested.load() )
                return;
            
            // Find the tab index for handleBackendEventDirect
            int tabIndex = -1;
            for( size_t i = 0; i < m_tabs.size(); i++ )
            {
                if( &m_tabs[i] == tab )
                {
                    tabIndex = static_cast<int>( i );
                    break;
                }
            }
            
            if( tabIndex < 0 )
                return;  // Tab not found (shouldn't happen, but be safe)
            
            // Pass tabIndex directly for parallel streaming safety
            // No need to set global m_streamingTabIndex - each event knows its tab
            handleBackendEventDirect( aEvent, tabIndex );
        } );
    } );

    // Run streaming in background thread
    // IMPORTANT: Use per-tab session/conversation IDs for parallel execution safety
    std::string message = aMessage.ToStdString();
    std::string traceFile = traceFilePath.ToStdString();
    std::string kicadFile = kicadFilePath.ToStdString();
    std::string sessionId = m_sessionId.ToStdString();          // App-global session ID (shared by all tabs)
    std::string conversationId = tab.conversationId.ToStdString(); // Per-tab conversation ID
    std::string mode = m_modeDropdown->GetModeLabel().Lower().ToStdString();
    std::string appType = GetAppType().ToStdString();
    std::string auth = authToken.ToStdString();
    std::string refresh = refreshToken.ToStdString();

    // Join previous request thread for this tab if still running (shouldn't happen, but be safe)
    if( tab.requestThread && tab.requestThread->joinable() )
    {
        tab.requestThread->detach(); // Detach old thread to prevent blocking UI
    }

    // Create per-tab request thread for TRUE parallel execution
    // Each tab uses its own backend client so multiple tabs can stream simultaneously
    // NOTE: Use conversationId instead of tabIndex to avoid stale index issues during tab closure/shutdown
    tab.requestThread = std::make_unique<std::thread>( [this, panelAlive, tabClient, message, traceFile, kicadFile, sessionId, conversationId,
                  mode, appType, auth, refresh]()
    {
        // Use per-tab backend client for parallel streaming
        AI_STREAM_RESULT result = tabClient->StreamChat(
                message, traceFile, kicadFile, sessionId, conversationId,
                mode, appType, auth, refresh );

        if( !panelAlive->load() )
            return;

        // Handle completion on UI thread
        // NOTE: Use conversationId to find tab, not tabIndex, because indices can shift if tabs are closed
        wxString convId = wxString::FromUTF8( conversationId );
        wxTheApp->CallAfter( [this, panelAlive, convId, result]()
        {
            if( !panelAlive->load() || m_isDestroying.load() )
                return;

            // Find tab by conversation ID (indices may have shifted if tabs were closed)
            TAB_DATA* tab = findTabByConversationId( convId );
            if( !tab )
                return;  // Tab was closed while streaming

            // Find the tab index for functions that need it
            int tabIndex = -1;
            for( size_t i = 0; i < m_tabs.size(); i++ )
            {
                if( &m_tabs[i] == tab )
                {
                    tabIndex = static_cast<int>( i );
                    break;
                }
            }
            
            if( tabIndex < 0 )
                return;  // Tab not found (shouldn't happen, but be safe)

            // Flush any remaining buffered text before finalizing (for this tab)
            flushStreamingBuffer( tabIndex );

            // Save AI response to local database using per-tab data
            wxString streamedResponse = tab->pendingStreamingResponse;
            
            if( !convId.IsEmpty() && !streamedResponse.IsEmpty() )
            {
                CONVERSATION_DB& db = CONVERSATION_DB::Instance();
                
                auto msg = db.SaveMessage( convId, wxT( "assistant" ), streamedResponse );
                if( !msg.has_value() )
                {
                    wxLogError( wxT( "Failed to save assistant message to database for conversation: %s" ), convId );
                }
            }

            // Clear per-tab streaming response for next turn
            tab->pendingStreamingResponse.Clear();

            bool success = ( result.status == "success" || result.status == "stopped" );
            wxString error;
            if( result.status == "error" )
                error = wxString::FromUTF8( result.error );
            
            // If request succeeded, hide any error banner (user may have upgraded or switched to Ask mode)
            if( success && m_quotaBanner && m_quotaBanner->IsShown() )
            {
                // Don't hide immediately - let fetchAndShowQuotaInfo decide based on current quota
            }
            
            // Check quota after successful request - only shows banner if quota is LOW
            if( success )
            {
                fetchAndShowQuotaInfo( false );  // false = not startup, only show if low quota
            }
            
            // CRITICAL: Reset per-tab streaming state BEFORE calling onBackendResponse
            // This allows isAnyTabStreaming() to return false so the button resets properly
            tab->isStreaming.store( false );
            tab->stopRequested.store( false );
            tab->streamingBuffer.Clear();
            tab->pendingDeltaCount = 0;
            
            // Stop idle status timer for this tab
            stopIdleStatusTimer( tabIndex );
            
            // Release file ownership for this tab (allows other tabs to edit these files)
            releaseFileOwnership( tabIndex );

            if( result.status == "auth_error" )
            {
                AUTH_MANAGER::Instance().SignOut();
                updateAuthUI();
                onBackendResponse( wxT( "Session expired. Please sign in again." ), false, tabIndex, false );
            }
            else if( result.status == "quota_exceeded" )
            {
                // Quota exhausted (402) - user hit daily/credit limit
                // Show banner with full info, no chat message needed
                wxString bannerMsg;
                if( !result.error.empty() )
                {
                    bannerMsg = wxString::FromUTF8( result.error );
                }
                else
                {
                    bannerMsg = wxT( "Request limit reached." );
                }
                bannerMsg += wxT( " Try Ask mode or upgrade." );
                
                showQuotaBanner( bannerMsg, true );  // true = show upgrade button
                // No chat message - banner has all the info
            }
            else if( result.status == "plan_restricted" )
            {
                // Plan restricted (403) - free user tried Agent/Plan mode
                // Auto-switch to Ask mode and show banner with full info
                wxString bannerMsg = wxT( "Free plan - Ask mode only. Upgrade for Agent & Plan." );
                
                showQuotaBanner( bannerMsg, true );  // true = show upgrade button
                
                // Auto-switch to Ask mode
                if( m_modeDropdown )
                {
                    m_modeDropdown->SetMode( AI_MODE::ASK );
                }
                // No chat message - banner has all the info, mode auto-switched
            }
            else
            {
                onBackendResponse( wxString::FromUTF8( result.response ), success, tabIndex, result.fileModified );
            }

            // Handle final cleanup after stream completes
            // Stop the batch update timer if running
            if( m_streamingBatchTimer && m_streamingBatchTimer->IsRunning() )
            {
                m_streamingBatchTimer->Stop();
            }

            // Clear any pending flags (they may have been consumed by batch timer)
            bool hadBatchPending = m_batchUpdatePending.exchange( false );
            bool hadFileModified = result.fileModified || tab->fileModifiedDuringStream.exchange( false );

            // ALWAYS flush pending conversions at stream end
            // This is critical because conversions are debounced and may not have happened yet
            bool conversionHappened = false;
            if( tab->toolExecutor )
            {
                conversionHappened = tab->toolExecutor->flushPendingConversion( true );
            }
            
            // Reload if: conversion happened OR batch was pending OR file was modified
            bool needsReload = conversionHappened || hadBatchPending || hadFileModified;
            
            if( needsReload )
            {
                wxString filePath = GetCurrentFileName();
                
                if( !filePath.IsEmpty() )
                {
                    // Use mutex to prevent concurrent reload operations
                    std::lock_guard<std::mutex> lock( m_reloadMutex );

                    // Capture state if not already captured
                    if( m_aiEditInProgress && !m_aiEditStateCaptured )
                    {
                        CaptureStateForAIEdit( filePath );
                        m_aiEditStateCaptured = true;
                    }

                    // Final reload and create undo entries
                    if( ReloadFromFile( filePath ) )
                    {
                        CompareAndCreateAIEditUndoEntries();
                        
                        // Autoplace fields for modified symbols
                        if( tab->toolExecutor )
                        {
                            std::set<std::string> modifiedUUIDs = tab->toolExecutor->GetModifiedSymbolUUIDs();
                            if( !modifiedUUIDs.empty() )
                            {
                                AutoplaceModifiedSymbols( modifiedUUIDs );
                                tab->toolExecutor->ClearModifiedSymbolUUIDs();
                            }
                        }
                        
                        // Auto-annotate all symbols after trace edits
                        AnnotateAllSymbols();
                        
                        // Save document to persist annotation changes
                        SaveDocument();
                        
                        // Only mark as saved if conversion succeeded (or no conversion was needed)
                        if( !conversionHappened || ( tab->toolExecutor && tab->toolExecutor->WasLastConversionSuccessful() ) )
                        {
                            MarkDocumentAsSaved();
                        }
                    }
                }
            }

            // Update conversation ID if returned
            
            // Note: Per-tab streaming state was already reset BEFORE onBackendResponse()
            // to ensure isAnyTabStreaming() returns false so the button resets properly
        } );
    } );
}


void AI_CHAT_PANEL_BASE::onStreamingText( const wxString& aText, bool aIsFirst, int aTabIndex )
{
    // Critical: Don't touch UI if panel is being destroyed (prevents crash from queued callbacks)
    if( m_isDestroying.load() )
        return;

    // Validate tab index (per-tab safety - uses passed index, not global)
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;
    
    TAB_DATA& tab = m_tabs[aTabIndex];
    
    // Safety check: Don't process if this tab's stream was stopped
    if( tab.stopRequested.load() )
        return;

    // Accumulate response in per-tab storage (not global - supports parallel streaming)
    if( aIsFirst )
    {
        tab.pendingStreamingResponse = aText;
    }
    else
    {
        tab.pendingStreamingResponse += aText;
    }
    
    tab.isStreaming.store( true );

    // Update THIS tab's content panel directly (TRUE independence)
    // No need to check if we're on the current tab - each tab has its own panel
    if( !tab.contentPanel )
        return;

    // Preserve focus on input box during streaming updates
    // (ScrollToBottom can steal focus)
    wxWindow* focusedWindow = wxWindow::FindFocus();
    bool inputHadFocus = ( focusedWindow == m_inputBox || ( m_inputBox && m_inputBox->IsDescendant( focusedWindow ) ) );

    // Hide both indicators when actual content arrives
    tab.contentPanel->HideStatusIndicator();
    tab.contentPanel->HideTypingIndicator();

    // Update this tab's chat history
    if( aIsFirst )
    {
        tab.contentPanel->AddAIMessage( aText );
    }
    else
    {
        tab.contentPanel->AppendToLastAIMessage( aText );
    }

    // Restore focus to input if it had focus before
    if( inputHadFocus && m_inputBox )
    {
        m_inputBox->SetFocus();
    }
}


void AI_CHAT_PANEL_BASE::bufferStreamingText( const wxString& aText, int aTabIndex )
{
    if( aText.IsEmpty() )
        return;

    // Validate tab index (uses passed index, not global - for parallel streaming safety)
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;
    
    TAB_DATA& tab = m_tabs[aTabIndex];

    // Accumulate text in per-tab buffer
    // (isFirstStreamingFlush is set at stream start and cleared after first flush)
    tab.streamingBuffer += aText;
    tab.pendingDeltaCount++;

    // Start timer if not running (shared timer is ok - flushStreamingBuffer checks per-tab)
    if( m_streamingFlushTimer && !m_streamingFlushTimer->IsRunning() )
    {
        m_streamingFlushTimer->Start( STREAMING_FLUSH_INTERVAL_MS, wxTIMER_ONE_SHOT );
    }

    // Flush immediately if threshold reached
    if( tab.pendingDeltaCount >= STREAMING_FLUSH_DELTA_COUNT )
    {
        flushStreamingBuffer( aTabIndex );
    }
}


void AI_CHAT_PANEL_BASE::flushStreamingBuffer( int aTabIndex )
{
    // Critical: Don't access UI if panel is being destroyed
    if( m_isDestroying.load() )
        return;
    
    // Use explicit tab index for parallel streaming safety
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;
    
    TAB_DATA& tab = m_tabs[aTabIndex];
    
    if( tab.streamingBuffer.IsEmpty() )
        return;

    // Stop timer if running
    if( m_streamingFlushTimer && m_streamingFlushTimer->IsRunning() )
    {
        m_streamingFlushTimer->Stop();
    }

    // Flush buffered content to UI
    wxString textToFlush = tab.streamingBuffer;
    bool isFirst = tab.isFirstStreamingFlush;

    // Clear per-tab buffer and reset state
    tab.streamingBuffer.Clear();
    tab.pendingDeltaCount = 0;
    tab.isFirstStreamingFlush = false;

    // Update UI with accumulated text (pass tab index for parallel streaming safety)
    onStreamingText( textToFlush, isFirst, aTabIndex );
}

void AI_CHAT_PANEL_BASE::flushStreamingBuffer()
{
    // Default overload - flush current streaming tab
    flushStreamingBuffer( m_streamingTabIndex );
}


void AI_CHAT_PANEL_BASE::onStreamingFlushTimer( wxTimerEvent& aEvent )
{
    // For parallel streaming safety, flush ALL tabs that have buffered content
    for( size_t i = 0; i < m_tabs.size(); ++i )
    {
        if( !m_tabs[i].streamingBuffer.IsEmpty() )
        {
            flushStreamingBuffer( static_cast<int>( i ) );
        }
    }
}


void AI_CHAT_PANEL_BASE::onStreamingStatus( const wxString& aStatus, int aTabIndex )
{
    // Critical: Don't touch UI if panel is being destroyed (prevents crash from queued callbacks)
    if( m_isDestroying.load() )
        return;
    
    // Validate tab index (uses passed index, not global - for parallel streaming safety)
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;
    
    TAB_DATA& tab = m_tabs[aTabIndex];
    
    // Safety check: Don't process if this tab's stream was stopped
    if( tab.stopRequested.load() )
        return;
    
    if( !tab.contentPanel )
        return;

    // Status messages (e.g., "Editing schematic...", "Searching...")
    // Update THIS tab's status indicator directly (TRUE independence)

    // Hide typing indicator - status text is now the indicator
    tab.contentPanel->HideTypingIndicator();

    if( !aStatus.IsEmpty() )
    {
        // Show as subtle status indicator - no bubble, small grey text
        tab.contentPanel->ShowStatusIndicator( aStatus );
    }
}


void AI_CHAT_PANEL_BASE::onBackendResponse( const wxString& aResponse, bool aSuccess, int aTabIndex, bool aFileModified )
{
    // Critical: Don't touch UI if panel is being destroyed (prevents crash from queued callbacks)
    if( m_isDestroying.load() )
        return;
    
    // Validate tab index (uses passed index, not global - for parallel streaming safety)
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;
    
    // Get the specific tab's content panel (TRUE independence - update its own panel)
    TAB_CONTENT_PANEL* contentPanel = getContentPanel( aTabIndex );
    
    if( contentPanel )
    {
        // Always hide status indicator and typing indicator when response completes
        contentPanel->HideStatusIndicator();
        contentPanel->HideTypingIndicator();

        // Check if this tab was streaming (already displayed text incrementally)
        // Use isFirstStreamingFlush - it's false after any streaming happened
        TAB_DATA& tab = m_tabs[aTabIndex];
        bool wasStreaming = !tab.isFirstStreamingFlush;  // false = streaming happened

        if( !wasStreaming )
        {
            // For non-streaming responses (rare), add the response as a new message
            if( !aResponse.IsEmpty() )
            {
                contentPanel->AddAIMessage( aResponse );
            }
        }
        else if( !aSuccess && !aResponse.IsEmpty() )
        {
            // If streaming started but an error occurred, add the error as a new message
            contentPanel->AddAIMessage( aResponse );
        }
    }

    // Reset AI edit tracking after response completes
    if( !aFileModified || !m_aiEditInProgress )
    {
        m_aiEditInProgress = false;
        m_aiEditStateCaptured = false;
    }

    // Switch Stop button back to Send button if authenticated
    bool isAuthenticated = false;
    try
    {
        isAuthenticated = AUTH_MANAGER::Instance().IsAuthenticated();
    }
    catch( ... )
    {
        // Auth manager error - leave disabled
    }

    if( isAuthenticated )
    {
        // Only switch button back to "Send" if NO tabs are streaming
        // (for parallel execution support)
        bool anyStreaming = isAnyTabStreaming();
        if( !anyStreaming )
        {
    updateButtonState( false );
        }
    m_inputBox->Enable();
    m_inputBox->SetFocus();
    }

    // Note: Per-tab streaming state is reset in the completion handler
    // of sendToBackendAsync for true parallel execution support.
}


void AI_CHAT_PANEL_BASE::updateButtonState( bool aIsStopMode )
{
    if( !m_sendButton )
        return;

    if( aIsStopMode )
    {
        m_sendButton->SetBitmap( KiBitmapBundle( BITMAPS::icon_stop, 24 ) );
        m_sendButton->SetLabel( wxEmptyString );
        m_sendButton->Enable();
        
        m_sendButton->Unbind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onSendMessage, this );
        m_sendButton->Bind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onStopRequest, this );
    }
    else
    {
        m_sendButton->SetBitmap( KiBitmapBundle( BITMAPS::icon_send, 24 ) );
        m_sendButton->SetLabel( wxEmptyString );
        m_sendButton->SetForegroundColour( wxNullColour );
        m_sendButton->SetBackgroundColour( wxNullColour );
        
        m_sendButton->Unbind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onStopRequest, this );
        m_sendButton->Bind( wxEVT_BUTTON, &AI_CHAT_PANEL_BASE::onSendMessage, this );
    }
    
    m_sendButton->Refresh();
}


void AI_CHAT_PANEL_BASE::onStopRequest( wxCommandEvent& aEvent )
{
    // Stop the current tab's stream (per-tab for true parallel execution)
    if( m_currentTabIndex >= 0 && m_currentTabIndex < static_cast<int>( m_tabs.size() ) )
    {
        TAB_DATA& tab = m_tabs[m_currentTabIndex];
        tab.stopRequested.store( true );
        tab.isStreaming.store( false );  // Mark this tab as no longer streaming
        
        // Stop this tab's backend client
        if( tab.backendClient )
            tab.backendClient->StopStream();

        // Stop idle status timer for this tab
        stopIdleStatusTimer( m_currentTabIndex );

        // Flush any buffered text for this specific tab
        flushStreamingBuffer( m_currentTabIndex );
    }

    // Only change button to Send if NO tabs are streaming (supports parallel execution)
    if( !isAnyTabStreaming() )
    {
    updateButtonState( false );
    }
}


// =========================================================================
// Tab Management Methods
// =========================================================================

void AI_CHAT_PANEL_BASE::onTabSelected( wxCommandEvent& aEvent )
{
    int newIndex = aEvent.GetInt();
    if( newIndex == m_currentTabIndex || newIndex < 0 )
        return;

    // Note: We don't stop streaming when switching tabs - this allows parallel tasks
    // The streaming callbacks check m_streamingTabIndex to avoid updating wrong tab's UI

    // Switch to the new tab (shows its panel, hides others)
    switchToTab( newIndex );
}


void AI_CHAT_PANEL_BASE::onNewTab( wxCommandEvent& aEvent )
{
    // Note: We don't stop streaming - allows parallel tasks across tabs
    // No save needed - each tab has its own persistent panel
    
    // Create new tab and switch to it
    int newIndex = createNewTab();
    if( newIndex >= 0 )
    {
        switchToTab( newIndex );
    }
}


void AI_CHAT_PANEL_BASE::onTabClose( wxCommandEvent& aEvent )
{
    int tabIndex = aEvent.GetInt();
    if( tabIndex < 0 || tabIndex >= static_cast<int>( m_tabs.size() ) )
        return;

    // Don't allow closing the last tab
    if( m_tabs.size() <= 1 )
        return;

    TAB_DATA& tabToClose = m_tabs[tabIndex];

    // If this tab is actively streaming, stop it first
    // (we can't send updates to a deleted tab)
    if( tabToClose.isStreaming.load() )
    {
        tabToClose.stopRequested.store( true );
        
        // Stop the per-tab backend client
        if( tabToClose.backendClient )
            tabToClose.backendClient->StopStream();
        
        // Wait briefly for stream to stop (non-blocking check)
        for( int i = 0; i < 10; ++i )
        {
            if( !tabToClose.isStreaming.load() )
                break;
            wxMilliSleep( 10 );
        }
        
        // Force cleanup even if stream didn't stop cleanly
        tabToClose.isStreaming.store( false );
    }
    
    // CRITICAL: Always detach the request thread before destroying TAB_DATA
    // Even if streaming completed, the thread may still be joinable
    // std::thread destructor calls std::terminate() if joinable
    if( tabToClose.requestThread && tabToClose.requestThread->joinable() )
        tabToClose.requestThread->detach();
    
    // Note: With per-tab streaming, we don't need to update global streaming state
    // Each tab manages its own streaming independently
    
    // Update m_streamingTabIndex if needed (for event routing)
    if( m_streamingTabIndex > tabIndex )
        m_streamingTabIndex--;

    // Save conversation to local DB before closing
    if( !m_tabs[tabIndex].conversationId.IsEmpty() )
    {
        // Messages are already saved incrementally, just remove from UI
    }

    // Cleanup the tab's content panel before removing tab data
    if( tabToClose.contentPanel )
    {
        m_tabContentSizer->Detach( tabToClose.contentPanel );
        tabToClose.contentPanel->Destroy();
        tabToClose.contentPanel = nullptr;
    }

    // Remove tab data
    m_tabs.erase( m_tabs.begin() + tabIndex );

    // Update tab bar
    m_tabBar->RemoveTab( tabIndex );

    // Adjust current index based on which tab was closed
    if( tabIndex < m_currentTabIndex )
    {
        // Closed a tab before the current one - shift index down
        m_currentTabIndex--;
    }
    else if( tabIndex == m_currentTabIndex )
    {
        // Closed the current tab - need to switch to another
        if( m_currentTabIndex >= static_cast<int>( m_tabs.size() ) )
            m_currentTabIndex = static_cast<int>( m_tabs.size() ) - 1;
    }
    // else: closed a tab after the current one - no adjustment needed

    if( m_currentTabIndex >= 0 )
        switchToTab( m_currentTabIndex );
    
    // Save tab state after closing a tab
    saveOpenTabs();
}


void AI_CHAT_PANEL_BASE::onHistorySelect( wxCommandEvent& aEvent )
{
    // Get conversation history from local DB
    CONVERSATION_DB& db = CONVERSATION_DB::Instance();
    
    wxString userId = wxEmptyString;
    if( AUTH_MANAGER::Instance().IsAuthenticated() )
    {
        AUTH_USER user = AUTH_MANAGER::Instance().GetCurrentUser();
        userId = user.id;
    }

    std::vector<CONVERSATION> conversations = db.ListConversations( userId, 20 );

    if( conversations.empty() )
    {
        wxMessageBox( wxT( "No conversation history found." ), wxT( "History" ), wxOK | wxICON_INFORMATION );
        return;
    }

    // Create popup menu with recent conversations
    wxMenu menu;
    int    menuId = ID_HISTORY_ITEM_BASE;

    // Map menu IDs to conversation IDs
    std::map<int, wxString> menuIdToConvId;

    for( const auto& conv : conversations )
    {
        wxString label = conv.title.IsEmpty() ? wxString( wxT( "Untitled" ) ) : conv.title;
        
        // Truncate long titles
        if( label.length() > 40 )
            label = label.Left( 37 ) + wxT( "..." );

        // Add date hint
        wxString dateHint = conv.updated_at.Left( 10 ); // YYYY-MM-DD
        label += wxT( "  (" ) + dateHint + wxT( ")" );

        menu.Append( menuId, label );
        menuIdToConvId[menuId] = conv.id;
        menuId++;
    }

    // Bind menu events - use async loading for smooth UX
    menu.Bind( wxEVT_COMMAND_MENU_SELECTED,
               [this, menuIdToConvId]( wxCommandEvent& evt )
               {
                   auto it = menuIdToConvId.find( evt.GetId() );
                   if( it != menuIdToConvId.end() )
                   {
                       loadConversationToTabAsync( it->second );
                   }
               } );

    // Show menu below history button
    wxRect histRect = m_tabBar->getHistoryButtonRect();
    wxPoint menuPos = m_tabBar->ClientToScreen( wxPoint( histRect.GetX(), histRect.GetBottom() ) );
    menuPos = ScreenToClient( menuPos );
    PopupMenu( &menu, menuPos );
}



void AI_CHAT_PANEL_BASE::switchToTab( int aTabIndex )
{
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;

    // Save current tab's draft input before switching
    if( m_currentTabIndex >= 0 && m_currentTabIndex < static_cast<int>( m_tabs.size() ) && m_inputBox )
    {
        m_tabs[m_currentTabIndex].draftInput = m_inputBox->GetValue();
    }

    // Hide all panels
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        if( m_tabs[i].contentPanel )
        {
            m_tabs[i].contentPanel->Hide();
        }
    }

    TAB_DATA& tab = m_tabs[aTabIndex];

    // Show the selected tab's panel
    if( tab.contentPanel )
    {
        tab.contentPanel->Show();

        // Load messages if this tab hasn't been loaded yet (e.g., restored tabs)
        if( !tab.messagesLoaded && !tab.conversationId.IsEmpty() )
        {
            loadMessagesForTab( aTabIndex );
        }
    }

    // Update tab bar selection
    if( m_tabBar )
    m_tabBar->SelectTab( aTabIndex );

    // Update global conversation ID for the current tab (session ID is app-global, doesn't change)
    m_conversationId = tab.conversationId;
    m_currentTabIndex = aTabIndex;

    // Restore this tab's draft input
    if( m_inputBox )
    {
        m_inputBox->SetValue( tab.draftInput );
    }

    // Update button state based on THIS tab's streaming state
    updateButtonState( tab.isStreaming.load() );
            
    // Re-layout the container
    m_tabContentContainer->Layout();
}


TAB_DATA* AI_CHAT_PANEL_BASE::findTabByConversationId( const wxString& aConversationId )
{
    if( aConversationId.IsEmpty() )
        return nullptr;
    
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        if( m_tabs[i].conversationId == aConversationId )
        {
            return &m_tabs[i];
        }
    }
    
    return nullptr;
}


void AI_CHAT_PANEL_BASE::loadMessagesForTab( int aTabIndex )
                {
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;

    TAB_DATA& tab = m_tabs[aTabIndex];
    
    // Skip if already loaded
    if( tab.messagesLoaded )
        return;
            
    // Use atomic compare-exchange to prevent duplicate loads (clean race condition handling)
    bool expected = false;
    if( !tab.isLoadingMessages.compare_exchange_strong( expected, true ) )
    {
        // Another thread is already loading - skip
        return;
    }
    
    if( !tab.contentPanel || tab.conversationId.IsEmpty() )
{
        tab.isLoadingMessages.store( false );  // Reset on early exit
        return;
    }
    
    // Show loading skeleton in this tab's panel
    tab.contentPanel->ShowLoadingSkeleton();
    
    // Load messages in background thread
    auto panelAlive = m_panelAlive;
    wxString convId = tab.conversationId;

    std::thread( [this, panelAlive, convId]()
    {
        if( !panelAlive->load() )
            return;
        
        // Load messages from DB
        CONVERSATION_DB& db = CONVERSATION_DB::Instance();
        std::vector<MESSAGE> messages = db.LoadMessages( convId );
        
        // Update UI on main thread
        // NOTE: Use convId to find the tab, not aTabIndex, because indices can shift if tabs are closed
        wxTheApp->CallAfter( [this, panelAlive, convId, messages]()
        {
            if( !panelAlive->load() || m_isDestroying.load() )
                return;

            // Find the tab by conversation ID (indices may have shifted)
            int tabIndex = -1;
            for( size_t i = 0; i < m_tabs.size(); i++ )
            {
                if( m_tabs[i].conversationId == convId )
                {
                    tabIndex = static_cast<int>( i );
                    break;
                }
            }
            
            if( tabIndex < 0 )
                return;  // Tab was closed while loading
            
            TAB_DATA& tab = m_tabs[tabIndex];
            if( !tab.contentPanel )
                return;
            
            // Hide loading skeleton and CLEAR before adding messages
            tab.contentPanel->HideLoadingSkeleton();
            tab.contentPanel->Clear();
                
            // Add messages to this tab's chat history
                for( const auto& msg : messages )
                {
                    if( msg.role == wxT( "user" ) )
                    tab.contentPanel->AddUserMessage( msg.content );
                    else if( msg.role == wxT( "assistant" ) )
                    tab.contentPanel->AddAIMessage( msg.content );
            }

            // If this tab is actively streaming, add the pending response
            if( tab.isStreaming.load() && !tab.pendingStreamingResponse.IsEmpty() )
            {
                tab.contentPanel->AddAIMessage( tab.pendingStreamingResponse );
            }
            
            // Mark as loaded (after successful load)
            tab.messagesLoaded = true;
            tab.isLoadingMessages.store( false );
        } );
    } ).detach();
}


int AI_CHAT_PANEL_BASE::createNewTab()
{
    if( static_cast<int>( m_tabs.size() ) >= CONVERSATION_TAB_BAR::MAX_TABS )
    {
        wxMessageBox( wxString::Format( wxT( "Maximum of %d tabs allowed. Please close a tab first." ),
                                        CONVERSATION_TAB_BAR::MAX_TABS ),
                      wxT( "Tab Limit" ), wxOK | wxICON_WARNING );
        return -1;
    }

    TAB_DATA newTab;
    newTab.conversationId = wxEmptyString;  // Will be set when first message is sent
    // NOTE: sessionId is global (m_sessionId), not per-tab
    newTab.title = wxT( "New Chat" );
    newTab.hasUnsavedChanges = false;
    newTab.messagesLoaded = true;  // New tabs don't need loading from DB
    
    // Create per-tab content panel (TRUE independence - each tab has its own chat history)
    newTab.contentPanel = new TAB_CONTENT_PANEL( m_tabContentContainer );
    m_tabContentSizer->Add( newTab.contentPanel, 1, wxEXPAND );
    newTab.contentPanel->Hide();  // Hidden until selected

    // Initialize backend client and DEDICATED tool executor for this tab (prevents deadlocks!)
    newTab.backendClient = std::make_unique<AI_BACKEND_CLIENT>( m_backendUrl.ToStdString() );
    newTab.toolExecutor = std::make_unique<AI_TOOL_EXECUTOR>();
    configureToolExecutor( newTab.toolExecutor.get() );  // Apply stored callbacks immediately
    newTab.backendClient->SetToolExecutor( newTab.toolExecutor.get() );

    m_tabs.push_back( std::move( newTab ) );
    int newIndex = static_cast<int>( m_tabs.size() ) - 1;

    // Add to tab bar
    if( m_tabBar )
        m_tabBar->AddTab( m_tabs[newIndex].conversationId, m_tabs[newIndex].title );

    // New tab starts with no conversation (will be created on first message)
    // Session ID is app-global, doesn't need to be set here
    m_conversationId = wxEmptyString;

    return newIndex;
}


bool AI_CHAT_PANEL_BASE::loadConversationToTab( const wxString& aConversationId )
{
    CONVERSATION_DB& db = CONVERSATION_DB::Instance();
    
    auto conv = db.LoadConversation( aConversationId );
    if( !conv.has_value() )
        return false;

    // Note: We don't stop streaming - allows parallel tasks across tabs

    // Check if this conversation is already open in a tab
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        if( m_tabs[i].conversationId == aConversationId )
        {
            // Switch to existing tab (no save needed - each tab has its own panel)
            switchToTab( static_cast<int>( i ) );
            return true;
        }
    }

    // Create new tab for this conversation
    // No save needed - each tab has its own persistent panel

    if( static_cast<int>( m_tabs.size() ) >= CONVERSATION_TAB_BAR::MAX_TABS )
    {
        // Close oldest tab to make room (not the current one)
        int tabToClose = ( m_currentTabIndex == 0 ) ? 1 : 0;
        m_tabs.erase( m_tabs.begin() + tabToClose );
        m_tabBar->RemoveTab( tabToClose );
        if( m_currentTabIndex > tabToClose )
            m_currentTabIndex--;
    }

    TAB_DATA newTab;
    newTab.conversationId = aConversationId;
    // NOTE: sessionId is global (m_sessionId) - shared by all tabs in this app session
    newTab.title = conv->title.IsEmpty() ? wxString( wxT( "Loaded Chat" ) ) : conv->title;
    newTab.hasUnsavedChanges = false;

    // Create per-tab content panel
    newTab.contentPanel = new TAB_CONTENT_PANEL( m_tabContentContainer );
    m_tabContentSizer->Add( newTab.contentPanel, 1, wxEXPAND );
    newTab.contentPanel->Hide();  // Hidden until selected

    // Initialize backend client and DEDICATED tool executor for this tab (prevents deadlocks!)
    newTab.backendClient = std::make_unique<AI_BACKEND_CLIENT>( m_backendUrl.ToStdString() );
    newTab.toolExecutor = std::make_unique<AI_TOOL_EXECUTOR>();
    configureToolExecutor( newTab.toolExecutor.get() );  // Apply stored callbacks immediately
    newTab.backendClient->SetToolExecutor( newTab.toolExecutor.get() );

    m_tabs.push_back( std::move( newTab ) );
    int newIndex = static_cast<int>( m_tabs.size() ) - 1;

    if( m_tabBar )
        m_tabBar->AddTab( m_tabs[newIndex].conversationId, m_tabs[newIndex].title );
    
    // Switch to the new tab (switchToTab handles message loading)
    m_currentTabIndex = -1;  // Reset so switchToTab actually switches
    switchToTab( newIndex );

    return true;
}


void AI_CHAT_PANEL_BASE::loadConversationToTabAsync( const wxString& aConversationId )
{
    // Check if already open in a tab (quick check, no DB access)
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        if( m_tabs[i].conversationId == aConversationId )
        {
            // Switch to existing tab (no save needed - each tab has its own panel)
            switchToTab( static_cast<int>( i ) );
            return;
        }
    }
    
    // NOTE: Don't show loading skeleton on current tab - we're creating a NEW tab
    // The new tab will show its own loading skeleton via loadMessagesForTab
    
    auto panelAlive = m_panelAlive;
    wxString convId = aConversationId;
    
    // Join previous conversation load thread if still running
    if( m_conversationLoadThread && m_conversationLoadThread->joinable() )
    {
        m_conversationLoadThread->detach(); // Detach old thread to prevent blocking UI
    }
    

    // Load conversation metadata in background
    m_conversationLoadThread = std::make_unique<std::thread>( [this, panelAlive, convId]() {
        if( !panelAlive->load() )
            return;
        
        // Load conversation from DB
        CONVERSATION_DB& db = CONVERSATION_DB::Instance();
        auto conv = db.LoadConversation( convId );
        
        if( !conv.has_value() )
        {
            // Conversation not found - just log and return (no UI to update since we didn't show a skeleton)
            return;
        }
        
        // Prepare tab data
        // NOTE: sessionId is global (m_sessionId), generated once at app start
        wxString title = conv->title.IsEmpty() ? wxString( wxT( "Loaded Chat" ) ) : conv->title;
        
        // Update UI on main thread
        wxTheApp->CallAfter( [this, panelAlive, convId, title]() {
            if( !panelAlive->load() || m_isDestroying.load() )
                return;
            
            // No save needed - each tab has its own persistent panel
            
            // Handle max tabs
            if( static_cast<int>( m_tabs.size() ) >= CONVERSATION_TAB_BAR::MAX_TABS )
            {
                int tabToClose = ( m_currentTabIndex == 0 ) ? 1 : 0;
                m_tabs.erase( m_tabs.begin() + tabToClose );
                m_tabBar->RemoveTab( tabToClose );
                if( m_currentTabIndex > tabToClose )
                    m_currentTabIndex--;
            }
            
            // Create new tab
            TAB_DATA newTab;
            newTab.conversationId = convId;
            // NOTE: sessionId is global (m_sessionId), not per-tab
            newTab.title = title;
            newTab.hasUnsavedChanges = false;
            
            // Create per-tab content panel
            newTab.contentPanel = new TAB_CONTENT_PANEL( m_tabContentContainer );
            m_tabContentSizer->Add( newTab.contentPanel, 1, wxEXPAND );
            newTab.contentPanel->Hide();  // Hidden until selected
            
            // Initialize backend client and DEDICATED tool executor for this tab (prevents deadlocks!)
            newTab.backendClient = std::make_unique<AI_BACKEND_CLIENT>( m_backendUrl.ToStdString() );
            newTab.toolExecutor = std::make_unique<AI_TOOL_EXECUTOR>();
            configureToolExecutor( newTab.toolExecutor.get() );  // Apply stored callbacks immediately
            newTab.backendClient->SetToolExecutor( newTab.toolExecutor.get() );
            
            m_tabs.push_back( std::move( newTab ) );
            int newIndex = static_cast<int>( m_tabs.size() ) - 1;
            
            if( m_tabBar )
                m_tabBar->AddTab( m_tabs[newIndex].conversationId, m_tabs[newIndex].title );
            
            // Switch to the new tab (switchToTab handles message loading)
            m_currentTabIndex = -1;  // Reset so switchToTab actually switches
            switchToTab( newIndex );
            
            // Save tab state after loading from history
            saveOpenTabs();
        } );
    } );
}


void AI_CHAT_PANEL_BASE::saveOpenTabs()
{
    if( m_tabs.empty() )
        return;
    
    // Use cached project path (safe for destructor - doesn't call pure virtual)
    wxString projectPath = m_cachedProjectPath;
    if( projectPath.IsEmpty() )
        projectPath = wxT( "Untitled" );
    
    // Build the list of open tabs
    std::vector<OPEN_TAB> openTabs;
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        // Only save tabs that have valid conversation IDs
        if( m_tabs[i].conversationId.IsEmpty() )
            continue;
        
        OPEN_TAB tab;
        tab.conversation_id = m_tabs[i].conversationId;
        tab.tab_order = static_cast<int>( i );
        tab.is_active = ( static_cast<int>( i ) == m_currentTabIndex );
        tab.project_file_path = projectPath;
        openTabs.push_back( tab );
    }
    
    // Save to database
    CONVERSATION_DB& db = CONVERSATION_DB::Instance();
    if( db.SaveOpenTabs( openTabs, projectPath ) )
    {
    }
    else
    {
        wxLogWarning( wxT( "AI: Failed to save open tabs" ) );
    }
}


bool AI_CHAT_PANEL_BASE::loadPersistedTabs()
{
    // Get project file path for scoping and cache it
    wxString projectPath = GetCurrentFileName();
    if( projectPath.IsEmpty() )
        projectPath = wxT( "Untitled" );
    
    // Update cached path (safe access for destructor)
    m_cachedProjectPath = projectPath;
    
    // Load from database
    CONVERSATION_DB& db = CONVERSATION_DB::Instance();
    std::vector<OPEN_TAB> openTabs = db.LoadOpenTabs( projectPath );
    
    if( openTabs.empty() )
    {
        return false;
    }
    
    // Reorder: Put active tab first (position 0) for better UX
    auto activeIt = std::find_if( openTabs.begin(), openTabs.end(),
                                  []( const OPEN_TAB& tab ) { return tab.is_active; } );
    if( activeIt != openTabs.end() && activeIt != openTabs.begin() )
    {
        OPEN_TAB activeTab = *activeIt;
        openTabs.erase( activeIt );
        openTabs.insert( openTabs.begin(), activeTab );
    }
    
    // Clear any existing tabs (the initial empty tab created during buildUI)
    // We need to do this BEFORE adding restored tabs
    while( !m_tabs.empty() )
    {
        if( m_tabs.back().contentPanel )
        {
            m_tabContentSizer->Detach( m_tabs.back().contentPanel );
            m_tabs.back().contentPanel->Destroy();
            m_tabs.back().contentPanel = nullptr;
        }
        m_tabs.pop_back();
    }
    
    // Process pending deletions before creating new panels
    wxYield();
    
    // Clear the tab bar
    while( m_tabBar && m_tabBar->GetTabCount() > 0 )
    {
        m_tabBar->RemoveTab( 0 );
    }
    
    // Active tab is now always at position 0 (we reordered above)
    int activeTabIndex = 0;
    
    // Create tabs from persisted data
    for( size_t i = 0; i < openTabs.size(); i++ )
    {
        const OPEN_TAB& openTab = openTabs[i];
        
        // Verify the conversation still exists in the database
        auto conv = db.LoadConversation( openTab.conversation_id );
        if( !conv.has_value() )
        {
            continue;
        }
        
        // Create the tab
        TAB_DATA newTab;
        newTab.conversationId = openTab.conversation_id;
        // NOTE: sessionId is global (m_sessionId) - generated once at app start, shared by all tabs
        newTab.title = conv->title.IsEmpty() ? wxString( wxT( "Loaded Chat" ) ) : conv->title;
        newTab.hasUnsavedChanges = false;
        
        // Create per-tab content panel
        newTab.contentPanel = new TAB_CONTENT_PANEL( m_tabContentContainer );
        m_tabContentSizer->Add( newTab.contentPanel, 1, wxEXPAND );
        newTab.contentPanel->Hide();  // Hidden until selected
        
        // Initialize backend client and DEDICATED tool executor for this tab (prevents deadlocks!)
        newTab.backendClient = std::make_unique<AI_BACKEND_CLIENT>( m_backendUrl.ToStdString() );
        newTab.toolExecutor = std::make_unique<AI_TOOL_EXECUTOR>();
        configureToolExecutor( newTab.toolExecutor.get() );  // Apply stored callbacks immediately
        newTab.backendClient->SetToolExecutor( newTab.toolExecutor.get() );
        
        m_tabs.push_back( std::move( newTab ) );
        int tabIndex = static_cast<int>( m_tabs.size() ) - 1;
        
        if( m_tabBar )
        {
            m_tabBar->AddTab( m_tabs[tabIndex].conversationId, m_tabs[tabIndex].title );
        }
        
        // Active tab is now always at index 0 (we reordered the vector before this loop)
        // No need to track it dynamically
    }
    
    // If we didn't restore any tabs, return false to let caller create a fresh one
    if( m_tabs.empty() )
    {
        return false;
    }
    
    // Switch to the previously active tab (switchToTab will load messages if needed)
    m_currentTabIndex = -1;  // Reset so switchToTab actually switches
    switchToTab( activeTabIndex );
    // Note: Don't call loadMessagesForTab here - switchToTab already does it
    
    return true;
}


void AI_CHAT_PANEL_BASE::HandleFileEditEvent( const AI_BACKEND_EVENT& aEvent, int aTabIndex )
{
    if( !aEvent.fileModified || !m_frame )
        return;

    wxString filePath = GetCurrentFileName();
    if( filePath.IsEmpty() )
        return;
    
    // Mark this file as modified by the specific tab (for per-tab reload tracking)
    markFileModifiedByTab( filePath, aTabIndex );

    // During streaming: Queue for batch update instead of immediate reload
    // This shows incremental changes every 500ms without overwhelming the UI
    bool anyStreaming = isAnyTabStreaming();
    
    if( anyStreaming )
    {
        m_batchUpdatePending.store( true );
        
        // Start batch timer if not already running (500ms batching interval)
        if( !m_streamingBatchTimer )
        {
            m_streamingBatchTimer = new wxTimer( this );
            Bind( wxEVT_TIMER, &AI_CHAT_PANEL_BASE::onStreamingBatchTimer, this,
                  m_streamingBatchTimer->GetId() );
        }
        
        if( !m_streamingBatchTimer->IsRunning() )
        {
            m_streamingBatchTimer->Start( 500, wxTIMER_ONE_SHOT );
        }
        
        return;
    }

    // Not streaming - perform immediate reload (for manual refresh, etc.)
    std::lock_guard<std::mutex> lock( m_reloadMutex );

    // Check memory before reload to prevent crashes
    wxMemorySize freeMemory = wxGetFreeMemory();
    if( freeMemory != wxNOT_FOUND && freeMemory < 500 * 1024 * 1024 )  // < 500 MB free
    {
        wxLogWarning( wxT( "AI: Low memory (%lld MB free), skipping reload" ),
                     ( freeMemory / (1024*1024) ).GetValue() );
        return;
    }

    // Check if user requested stop on any tab
    if( isAnyTabStopRequested() )
    {
        return;
    }

    // Capture state before reload if not already captured
    if( m_aiEditInProgress && !m_aiEditStateCaptured )
    {
        CaptureStateForAIEdit( filePath );
        m_aiEditStateCaptured = true;
    }

    // CRITICAL: Flush pending conversions before reload
    // Conversion is queued/debounced, so we must flush before loading the kicad file
    // Get the tab that triggered this event
    if( aTabIndex >= 0 && aTabIndex < static_cast<int>( m_tabs.size() ) && m_tabs[aTabIndex].toolExecutor )
    {
        m_tabs[aTabIndex].toolExecutor->flushPendingConversion( true );
    }

    // Direct reload (no timer needed when not streaming)
    if( ReloadFromFile( filePath ) )
    {
        CompareAndCreateAIEditUndoEntries();
        
        // Autoplace fields for modified symbols
        if( aTabIndex >= 0 && aTabIndex < static_cast<int>( m_tabs.size() ) && m_tabs[aTabIndex].toolExecutor )
        {
            std::set<std::string> modifiedUUIDs = m_tabs[aTabIndex].toolExecutor->GetModifiedSymbolUUIDs();
            if( !modifiedUUIDs.empty() )
            {
                AutoplaceModifiedSymbols( modifiedUUIDs );
                m_tabs[aTabIndex].toolExecutor->ClearModifiedSymbolUUIDs();
            }
        }
        
        // Auto-annotate all symbols after trace edits
        AnnotateAllSymbols();
        
        // Save document to persist annotation changes
        SaveDocument();
        
        // Only mark as saved if conversion succeeded
        bool conversionOk = true;
        if( aTabIndex >= 0 && aTabIndex < static_cast<int>( m_tabs.size() ) && m_tabs[aTabIndex].toolExecutor )
        {
            conversionOk = m_tabs[aTabIndex].toolExecutor->WasLastConversionSuccessful();
        }
        
        if( conversionOk )
        {
            MarkDocumentAsSaved();
        }
        else
        {
            wxString convError = ( aTabIndex >= 0 && aTabIndex < static_cast<int>( m_tabs.size() ) && m_tabs[aTabIndex].toolExecutor ) ? 
                wxString::FromUTF8( m_tabs[aTabIndex].toolExecutor->GetLastConversionError() ) : 
                wxString( wxT( "Unknown error" ) );
            wxLogWarning( wxT( "AI: Conversion failed, not marking as saved: %s" ), convError );
        }
    }
    else
    {
        wxLogWarning( wxT( "AI: Reload failed for: %s" ), filePath );
    }
}


void AI_CHAT_PANEL_BASE::onStreamingBatchTimer( wxTimerEvent& aEvent )
{
    // Safety check: Only process if any tab is still streaming
    if( !isAnyTabStreaming() )
    {
        return;
    }

    // Check if batch update is actually pending
    if( !m_batchUpdatePending.exchange( false ) )
    {
        return;
    }

    wxString filePath = GetCurrentFileName();
    if( filePath.IsEmpty() )
    {
        wxLogWarning( wxT( "AI: Batch update - no current file" ) );
        return;
    }

    // Check memory before reload
    wxMemorySize freeMemory = wxGetFreeMemory();
    if( freeMemory != wxNOT_FOUND && freeMemory < 500 * 1024 * 1024 )
    {
        wxLogWarning( wxT( "AI: Low memory (%lld MB free), skipping batch update" ),
                     ( freeMemory / (1024*1024) ).GetValue() );
        return;
    }

    // CRITICAL: Flush pending conversions BEFORE reload
    // This ensures trace_pcb -> kicad_pcb conversion happens before KiCad loads the file
    // Flush conversions for ALL streaming tabs (not just current)
    bool conversionHappened = false;
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        if( m_tabs[i].isStreaming.load() && m_tabs[i].toolExecutor )
    {
            bool tabConversion = m_tabs[i].toolExecutor->flushPendingConversion( true );
            conversionHappened = conversionHappened || tabConversion;
        }
    }

    // Capture state before first reload during AI edit
    if( m_aiEditInProgress && !m_aiEditStateCaptured )
    {
        CaptureStateForAIEdit( filePath );
        m_aiEditStateCaptured = true;
    }

    // Perform the reload (safe during streaming with proper batching)
    if( ReloadFromFile( filePath ) )
    {
        CompareAndCreateAIEditUndoEntries();
        
        // Autoplace fields for modified symbols (check all streaming tabs)
        for( size_t i = 0; i < m_tabs.size(); i++ )
        {
            if( m_tabs[i].isStreaming.load() && m_tabs[i].toolExecutor )
            {
                std::set<std::string> modifiedUUIDs = m_tabs[i].toolExecutor->GetModifiedSymbolUUIDs();
                if( !modifiedUUIDs.empty() )
                {
                    AutoplaceModifiedSymbols( modifiedUUIDs );
                    m_tabs[i].toolExecutor->ClearModifiedSymbolUUIDs();
                }
            }
        }
        
        // Auto-annotate all symbols after trace edits
        AnnotateAllSymbols();
        
        // Save document to persist annotation changes
        SaveDocument();
    }
}


void AI_CHAT_PANEL_BASE::onIdleStatusTimer( wxTimerEvent& aEvent )
{
    // Critical: Don't touch UI if panel is being destroyed
    if( m_isDestroying.load() )
        return;

    // Find which tab's timer fired by checking the timer ID
    int tabIndex = -1;
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        if( m_tabs[i].idleStatusTimer && m_tabs[i].idleStatusTimer->GetId() == aEvent.GetId() )
        {
            tabIndex = static_cast<int>( i );
            break;
        }
    }

    if( tabIndex < 0 )
        return;

    TAB_DATA& tab = m_tabs[tabIndex];

    // Only show status if this tab is still streaming
    if( !tab.isStreaming.load() || tab.stopRequested.load() )
        return;

    // Track how many times the idle timer has fired for this stream
    // We use a static map, but reset it when pendingStreamingResponse is empty (new stream)
    static std::map<int, int> tabIdleCount;
    
    // If no response yet, this is a fresh stream - reset counter
    if( tab.pendingStreamingResponse.IsEmpty() && tab.streamingBuffer.IsEmpty() )
    {
        tabIdleCount[tabIndex] = 0;
    }
    
    int& idleCount = tabIdleCount[tabIndex];
    
    if( idleCount == 0 )
    {
        // First idle timeout - just show typing indicator (the dots animation)
        if( tab.contentPanel )
        {
            tab.contentPanel->ShowTypingIndicator();
        }
    }
    else
    {
        // Subsequent idle timeouts - rotate through friendly messages
        static const wxString idleMessages[] = {
            wxT( "Thinking..." ),
            wxT( "Planning next steps..." ),
            wxT( "Still working on it..." ),
            wxT( "Analyzing..." ),
            wxT( "Processing..." )
        };
        
        wxString statusMsg = idleMessages[(idleCount - 1) % 5];

        if( tab.contentPanel )
        {
            tab.contentPanel->ShowStatusIndicator( statusMsg );
        }
    }
    
    idleCount++;
}


void AI_CHAT_PANEL_BASE::resetIdleStatusTimer( int aTabIndex )
{
    // Validate tab index
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;

    TAB_DATA& tab = m_tabs[aTabIndex];

    // Only reset if this tab is streaming
    if( !tab.isStreaming.load() )
        return;

    // Create timer if it doesn't exist
    if( !tab.idleStatusTimer )
    {
        tab.idleStatusTimer = new wxTimer( this, wxID_ANY );
        Bind( wxEVT_TIMER, &AI_CHAT_PANEL_BASE::onIdleStatusTimer, this, tab.idleStatusTimer->GetId() );
    }

    // Stop and restart the timer (resets the 2-second countdown)
    if( tab.idleStatusTimer->IsRunning() )
    {
        tab.idleStatusTimer->Stop();
    }
    tab.idleStatusTimer->Start( IDLE_STATUS_TIMEOUT_MS, wxTIMER_ONE_SHOT );
}


void AI_CHAT_PANEL_BASE::stopIdleStatusTimer( int aTabIndex )
{
    // Validate tab index
    if( aTabIndex < 0 || aTabIndex >= static_cast<int>( m_tabs.size() ) )
        return;

    TAB_DATA& tab = m_tabs[aTabIndex];

    // Stop the timer if it exists and is running
    if( tab.idleStatusTimer )
    {
        if( tab.idleStatusTimer->IsRunning() )
        {
            tab.idleStatusTimer->Stop();
        }
    }
}


void AI_CHAT_PANEL_BASE::onReloadDebounceTimer( wxTimerEvent& aEvent )
{
    std::lock_guard<std::mutex> lock( m_reloadMutex );
    
    // CRITICAL: Check if already reloading (prevents COM reentrancy)
    if( m_reloadInProgress.load() )
    {
        m_reloadPending.store( true );  // Re-queue
        // Do NOT start timer here - will be handled after current reload completes
        return;
    }
    
    wxString pathToReload = m_pendingReloadPath;
    m_reloadPending.store( false );
    
    // Check if user cancelled during debounce
    if( isAnyTabStopRequested() )
    {
        m_reloadInProgress.store( false );
        return;
    }
    
    // Mark reload in progress before starting
    m_reloadInProgress.store( true );
    
    // CRITICAL: Flush pending conversions before reload
    // This ensures trace_sch -> kicad_sch conversion completes before we load the file
    // Flush conversions for ALL tabs
    bool conversionHappened = false;
    for( size_t i = 0; i < m_tabs.size(); i++ )
    {
        if( m_tabs[i].toolExecutor )
        {
            bool tabConversion = m_tabs[i].toolExecutor->flushPendingConversion( true );
            conversionHappened = conversionHappened || tabConversion;
        }
    }
    
    // Perform the actual reload on main thread (we're already on main thread via timer)
    bool success = ReloadFromFile( pathToReload );
    
    if( success )
    {
        // Create undo entries after successful reload
        CompareAndCreateAIEditUndoEntries();
        
        // Autoplace fields for modified symbols (check all tabs)
        for( size_t i = 0; i < m_tabs.size(); i++ )
        {
            if( m_tabs[i].toolExecutor )
            {
                std::set<std::string> modifiedUUIDs = m_tabs[i].toolExecutor->GetModifiedSymbolUUIDs();
                if( !modifiedUUIDs.empty() )
                {
                    AutoplaceModifiedSymbols( modifiedUUIDs );
                    m_tabs[i].toolExecutor->ClearModifiedSymbolUUIDs();
                }
            }
        }
        
        // Auto-annotate all symbols after trace edits
        AnnotateAllSymbols();
        
        // Save document to persist annotation changes
        SaveDocument();
        
        // Check if all conversions succeeded
        bool allConversionsOk = true;
        for( size_t i = 0; i < m_tabs.size(); i++ )
        {
            if( m_tabs[i].toolExecutor && !m_tabs[i].toolExecutor->WasLastConversionSuccessful() )
            {
                allConversionsOk = false;
                wxString convError = wxString::FromUTF8( m_tabs[i].toolExecutor->GetLastConversionError() );
                wxLogWarning( wxT( "AI: Tab %zu conversion failed: %s" ), i, convError );
            }
        }
        
        if( allConversionsOk )
        {
            MarkDocumentAsSaved();
        }
        else
        {
            wxLogWarning( wxT( "AI: Some conversions failed, not marking as saved" ) );
        }
    }
    else
    {
        wxLogWarning( wxT( "AI: Reload failed for: %s" ), pathToReload );
    }
    
    // Mark reload complete
    m_reloadInProgress.store( false );
    
    // If another reload was queued during this one, start timer for it
    if( m_reloadPending.load() && !m_pendingReloadPath.IsEmpty() )
    {
        m_reloadInProgress.store( true );
        m_reloadDebounceTimer->Start( 1000, wxTIMER_ONE_SHOT );
    }
}
