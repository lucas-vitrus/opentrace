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

#include "chat_message_panel.h"
#include <widgets/html_window.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/settings.h>
#include <wx/dcclient.h>
#include <wx/button.h>
#include <wx/timer.h>
#include <wx/scrolwin.h>
#include <wx/log.h>
#include <wx/graphics.h>
#include <wx/regex.h>
#include <kiplatform/ui.h>
#include <string_utils.h>
#include <cmath>
#include <chrono>

// Expandable section implementation
EXPANDABLE_SECTION::EXPANDABLE_SECTION( wxWindow* aParent, const wxString& aSummary,
                                        const std::vector<wxString>& aItems ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_expanded( false ),
        m_summary( aSummary ),
        m_items( aItems ),
        m_toggleButton( nullptr ),
        m_expandedText( nullptr ),
        m_sizer( nullptr )
{
    SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );

    m_sizer = new wxBoxSizer( wxVERTICAL );

    // Create toggle button with summary text
    m_toggleButton =
            new wxButton( this, wxID_ANY, m_summary + wxT( " v" ), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE );
    m_toggleButton->SetBackgroundColour( GetBackgroundColour() );
    m_toggleButton->SetCursor( wxCursor( wxCURSOR_HAND ) );

    // Style the button to look like a link
    wxFont font = m_toggleButton->GetFont();
    font.SetUnderlined( true );
    m_toggleButton->SetFont( font );
    m_toggleButton->SetForegroundColour( wxColour( 0, 100, 200 ) );

    m_sizer->Add( m_toggleButton, 0, wxALL, 4 );

    // Create expanded text (initially hidden)
    wxString expandedContent;
    for( const wxString& item : m_items )
    {
        expandedContent += item + wxT( "\n" );
    }
    m_expandedText = new wxStaticText( this, wxID_ANY, expandedContent );
    m_expandedText->Hide();
    m_sizer->Add( m_expandedText, 0, wxLEFT | wxBOTTOM, 8 );

    SetSizer( m_sizer );

    m_toggleButton->Bind( wxEVT_BUTTON, &EXPANDABLE_SECTION::onToggleClick, this );
}


void EXPANDABLE_SECTION::SetExpanded( bool aExpanded )
{
    if( m_expanded != aExpanded )
    {
        m_expanded = aExpanded;
        updateLayout();
    }
}


void EXPANDABLE_SECTION::Toggle()
{
    SetExpanded( !m_expanded );
}


void EXPANDABLE_SECTION::onToggleClick( wxCommandEvent& aEvent )
{
    Toggle();

    // Notify parent to update layout
    wxWindow* parent = GetParent();
    if( parent )
    {
        parent->Layout();
        parent->Refresh();

        // If parent is a scrolled window, update virtual size
        wxScrolledWindow* scrolled = dynamic_cast<wxScrolledWindow*>( parent );
        if( scrolled )
        {
            scrolled->FitInside();
        }
    }
}


void EXPANDABLE_SECTION::updateLayout()
{
    if( m_expanded )
    {
        m_toggleButton->SetLabel( wxT( "See less ^" ) );
        m_expandedText->Show();
    }
    else
    {
        m_toggleButton->SetLabel( m_summary + wxT( " v" ) );
        m_expandedText->Hide();
    }

    Layout();
    Refresh();
}


// Typing indicator implementation (iMessage-style animated dots)
TYPING_INDICATOR_PANEL::TYPING_INDICATOR_PANEL( wxWindow* aParent ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxSize( 60, 36 ), wxBORDER_NONE ),
        m_timer( nullptr ),
        m_dotIndex( 0 ),
        m_running( false )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );

    m_timer = new wxTimer( this );
    Bind( wxEVT_PAINT, &TYPING_INDICATOR_PANEL::onPaint, this );
    Bind( wxEVT_TIMER, &TYPING_INDICATOR_PANEL::onTimer, this );
}


TYPING_INDICATOR_PANEL::~TYPING_INDICATOR_PANEL()
{
    Stop();
    delete m_timer;
}


void TYPING_INDICATOR_PANEL::Start()
{
    if( !m_running )
    {
        m_running = true;
        m_dotIndex = 0;
        m_timer->Start( 400 ); // Animate every 400ms
        Refresh();
    }
}


void TYPING_INDICATOR_PANEL::Stop()
{
    if( m_running )
    {
        m_running = false;
        m_timer->Stop();
    }
}


void TYPING_INDICATOR_PANEL::onTimer( wxTimerEvent& aEvent )
{
    m_dotIndex = ( m_dotIndex + 1 ) % 3;
    Refresh();
}


void TYPING_INDICATOR_PANEL::onPaint( wxPaintEvent& aEvent )
{
    wxPaintDC dc( this );
    wxRect    rect = GetClientRect();

    // Get theme colors
    bool     isDark = KIPLATFORM::UI::IsDarkTheme();
    wxColour bgColor = isDark ? wxColour( 60, 60, 65 ) : wxColour( 230, 230, 235 );
    wxColour dotInactive = isDark ? wxColour( 100, 100, 105 ) : wxColour( 180, 180, 185 );
    wxColour dotActive = isDark ? wxColour( 180, 180, 185 ) : wxColour( 100, 100, 105 );

    // Draw rounded background bubble
    dc.SetBrush( wxBrush( bgColor ) );
    dc.SetPen( *wxTRANSPARENT_PEN );

    int radius = 12;
    dc.DrawRoundedRectangle( rect, radius );

    // Draw three dots with animation
    int dotRadius = 4;
    int spacing = 12;
    int totalWidth = 3 * ( dotRadius * 2 ) + 2 * spacing;
    int startX = ( rect.GetWidth() - totalWidth ) / 2 + dotRadius;
    int centerY = rect.GetHeight() / 2;

    for( int i = 0; i < 3; i++ )
    {
        int x = startX + i * ( dotRadius * 2 + spacing );
        int y = centerY;

        // Animate: active dot "bounces" up slightly
        if( m_running && i == m_dotIndex )
        {
            y -= 3;
            dc.SetBrush( wxBrush( dotActive ) );
        }
        else
        {
            dc.SetBrush( wxBrush( dotInactive ) );
        }

        dc.DrawCircle( x, y, dotRadius );
    }
}


// Status indicator implementation (subtle text like Cursor's "Thought for 38s")
STATUS_INDICATOR_PANEL::STATUS_INDICATOR_PANEL( wxWindow* aParent, const wxString& aStatus ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_status( aStatus ),
        m_statusText( nullptr )
{
    // Transparent background - no bubble
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );

    wxBoxSizer* sizer = new wxBoxSizer( wxHORIZONTAL );

    // Create subtle grey text
    m_statusText = new wxStaticText( this, wxID_ANY, m_status );

    // Style: smaller font, grey color
    wxFont font = m_statusText->GetFont();
    font.SetPointSize( font.GetPointSize() - 2 ); // Smaller than normal
    m_statusText->SetFont( font );

    // Grey color for subtle appearance
    bool     isDark = KIPLATFORM::UI::IsDarkTheme();
    wxColour textColor = isDark ? wxColour( 140, 140, 145 ) : wxColour( 110, 110, 115 );
    m_statusText->SetForegroundColour( textColor );

    sizer->Add( m_statusText, 0, wxALL, 4 );
    SetSizer( sizer );

    Bind( wxEVT_PAINT, &STATUS_INDICATOR_PANEL::onPaint, this );
}


void STATUS_INDICATOR_PANEL::SetStatus( const wxString& aStatus )
{
    m_status = aStatus;
    if( m_statusText )
    {
        m_statusText->SetLabel( m_status );
        Layout();
    }
}


void STATUS_INDICATOR_PANEL::onPaint( wxPaintEvent& aEvent )
{
    wxPaintDC dc( this );
    wxSize size = GetClientSize();
    
    // Fill with parent's background color for seamless appearance
    dc.SetBrush( wxBrush( GetBackgroundColour() ) );
    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.DrawRectangle( 0, 0, size.GetWidth(), size.GetHeight() );
}


// Shimmer skeleton implementation (loading placeholder with animated gradient)
SHIMMER_SKELETON_PANEL::SHIMMER_SKELETON_PANEL( wxWindow* aParent ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxSize( -1, 180 ), wxBORDER_NONE ),
        m_timer( nullptr ),
        m_shimmerOffset( 0.0f ),
        m_running( false )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );

    m_timer = new wxTimer( this );
    Bind( wxEVT_PAINT, &SHIMMER_SKELETON_PANEL::onPaint, this );
    Bind( wxEVT_TIMER, &SHIMMER_SKELETON_PANEL::onTimer, this );
}


SHIMMER_SKELETON_PANEL::~SHIMMER_SKELETON_PANEL()
{
    Stop();
    delete m_timer;
}


void SHIMMER_SKELETON_PANEL::Start()
{
    if( !m_running )
    {
        m_running = true;
        m_shimmerOffset = 0.0f;
        m_timer->Start( 30 ); // ~33 FPS for smooth animation
    }
}


void SHIMMER_SKELETON_PANEL::Stop()
{
    if( m_running )
    {
        m_timer->Stop();
        m_running = false;
    }
}


void SHIMMER_SKELETON_PANEL::onTimer( wxTimerEvent& aEvent )
{
    m_shimmerOffset += 0.03f; // Speed of shimmer
    if( m_shimmerOffset > 2.0f )
        m_shimmerOffset = 0.0f;
    Refresh();
}


void SHIMMER_SKELETON_PANEL::onPaint( wxPaintEvent& aEvent )
{
    wxPaintDC dc( this );
    wxSize    size = GetClientSize();

    // Get theme colors
    wxColour bgColor = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
    int      luminance = ( bgColor.Red() * 299 + bgColor.Green() * 587 + bgColor.Blue() * 114 ) / 1000;
    bool     isDark = luminance < 128;

    // Skeleton box colors
    wxColour baseColor, shimmerColor;
    if( isDark )
    {
        baseColor = wxColour( 45, 45, 50 );       // Dark grey base
        shimmerColor = wxColour( 65, 65, 70 );    // Lighter shimmer
    }
    else
    {
        baseColor = wxColour( 230, 230, 235 );    // Light grey base
        shimmerColor = wxColour( 245, 245, 250 ); // White-ish shimmer
    }

    // Fill background
    dc.SetBrush( wxBrush( bgColor ) );
    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.DrawRectangle( 0, 0, size.GetWidth(), size.GetHeight() );

    // Define skeleton boxes (simulating chat messages)
    // User message placeholder (right-aligned, smaller)
    struct SkeletonBox
    {
        int  x, y, w, h;
        bool isUser;
    };

    int margin = 16;
    int boxRadius = 12;

    std::vector<SkeletonBox> boxes = {
        // First "user" message (right side)
        { size.GetWidth() - 180 - margin, 12, 180, 40, true },
        // First "AI" message (left side, larger)
        { margin, 64, 280, 32, false },
        { margin, 100, 220, 32, false },
        // Second "user" message
        { size.GetWidth() - 140 - margin, 144, 140, 32, true }
    };

    // Draw each skeleton box with shimmer effect
    for( const auto& box : boxes )
    {
        // Calculate shimmer position for this box
        float boxCenter = ( box.x + box.w / 2.0f ) / size.GetWidth();
        float shimmerPos = m_shimmerOffset - boxCenter;

        // Clamp shimmer to box range
        float shimmerIntensity = 0.0f;
        if( shimmerPos > -0.3f && shimmerPos < 0.3f )
        {
            // Smooth falloff from center
            shimmerIntensity = 1.0f - ( std::abs( shimmerPos ) / 0.3f );
            shimmerIntensity = shimmerIntensity * shimmerIntensity; // Ease
        }

        // Blend base color with shimmer
        wxColour boxColor(
            static_cast<unsigned char>( baseColor.Red() + ( shimmerColor.Red() - baseColor.Red() ) * shimmerIntensity ),
            static_cast<unsigned char>( baseColor.Green() + ( shimmerColor.Green() - baseColor.Green() ) * shimmerIntensity ),
            static_cast<unsigned char>( baseColor.Blue() + ( shimmerColor.Blue() - baseColor.Blue() ) * shimmerIntensity )
        );

        dc.SetBrush( wxBrush( boxColor ) );
        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.DrawRoundedRectangle( box.x, box.y, box.w, box.h, boxRadius );
    }
}


// Helper function to convert LaTeX math notation to Unicode
// This is a fallback for when the AI uses LaTeX despite being told to use Unicode
static wxString ConvertLatexToUnicode( const wxString& aText )
{
    wxString result = aText;
    
    // Remove LaTeX delimiters: $...$ and $$...$$
    // Replace inline math $...$ with just the content
    wxRegEx inlineMath( wxT( "\\$([^$]+)\\$" ) );
    while( inlineMath.Matches( result ) )
    {
        wxString match = inlineMath.GetMatch( result, 1 );
        inlineMath.ReplaceFirst( &result, match );
    }
    
    // Greek letters
    result.Replace( wxT( "\\alpha" ), wxT( "α" ) );
    result.Replace( wxT( "\\beta" ), wxT( "β" ) );
    result.Replace( wxT( "\\gamma" ), wxT( "γ" ) );
    result.Replace( wxT( "\\delta" ), wxT( "δ" ) );
    result.Replace( wxT( "\\Delta" ), wxT( "Δ" ) );
    result.Replace( wxT( "\\epsilon" ), wxT( "ε" ) );
    result.Replace( wxT( "\\theta" ), wxT( "θ" ) );
    result.Replace( wxT( "\\lambda" ), wxT( "λ" ) );
    result.Replace( wxT( "\\mu" ), wxT( "μ" ) );
    result.Replace( wxT( "\\pi" ), wxT( "π" ) );
    result.Replace( wxT( "\\Pi" ), wxT( "Π" ) );
    result.Replace( wxT( "\\sigma" ), wxT( "σ" ) );
    result.Replace( wxT( "\\Sigma" ), wxT( "Σ" ) );
    result.Replace( wxT( "\\tau" ), wxT( "τ" ) );
    result.Replace( wxT( "\\phi" ), wxT( "φ" ) );
    result.Replace( wxT( "\\omega" ), wxT( "ω" ) );
    result.Replace( wxT( "\\Omega" ), wxT( "Ω" ) );
    
    // Math operators
    result.Replace( wxT( "\\times" ), wxT( "×" ) );
    result.Replace( wxT( "\\cdot" ), wxT( "·" ) );
    result.Replace( wxT( "\\div" ), wxT( "÷" ) );
    result.Replace( wxT( "\\pm" ), wxT( "±" ) );
    result.Replace( wxT( "\\mp" ), wxT( "∓" ) );
    result.Replace( wxT( "\\leq" ), wxT( "≤" ) );
    result.Replace( wxT( "\\geq" ), wxT( "≥" ) );
    result.Replace( wxT( "\\neq" ), wxT( "≠" ) );
    result.Replace( wxT( "\\approx" ), wxT( "≈" ) );
    result.Replace( wxT( "\\equiv" ), wxT( "≡" ) );
    result.Replace( wxT( "\\propto" ), wxT( "∝" ) );
    result.Replace( wxT( "\\infty" ), wxT( "∞" ) );
    result.Replace( wxT( "\\sum" ), wxT( "Σ" ) );
    result.Replace( wxT( "\\prod" ), wxT( "Π" ) );
    result.Replace( wxT( "\\int" ), wxT( "∫" ) );
    result.Replace( wxT( "\\partial" ), wxT( "∂" ) );
    result.Replace( wxT( "\\nabla" ), wxT( "∇" ) );
    result.Replace( wxT( "\\rightarrow" ), wxT( "→" ) );
    result.Replace( wxT( "\\leftarrow" ), wxT( "←" ) );
    result.Replace( wxT( "\\Rightarrow" ), wxT( "⇒" ) );
    result.Replace( wxT( "\\Leftarrow" ), wxT( "⇐" ) );
    result.Replace( wxT( "\\leftrightarrow" ), wxT( "↔" ) );
    
    // Square root: \sqrt{x} -> √x or √(x)
    wxRegEx sqrtPattern( wxT( "\\\\sqrt\\{([^}]+)\\}" ) );
    while( sqrtPattern.Matches( result ) )
    {
        wxString content = sqrtPattern.GetMatch( result, 1 );
        wxString replacement = wxT( "√(" ) + content + wxT( ")" );
        sqrtPattern.ReplaceFirst( &result, replacement );
    }
    
    // Simple fractions: \frac{a}{b} -> (a)/(b) or a/b for simple cases
    wxRegEx fracPattern( wxT( "\\\\frac\\{([^}]+)\\}\\{([^}]+)\\}" ) );
    while( fracPattern.Matches( result ) )
    {
        wxString num = fracPattern.GetMatch( result, 1 );
        wxString den = fracPattern.GetMatch( result, 2 );
        wxString replacement;
        // Simple single-char fractions don't need parentheses
        if( num.length() == 1 && den.length() == 1 )
            replacement = num + wxT( "/" ) + den;
        else
            replacement = wxT( "(" ) + num + wxT( ")/(" ) + den + wxT( ")" );
        fracPattern.ReplaceFirst( &result, replacement );
    }
    
    // Superscripts: ^{2} -> ² etc, or ^2 -> ²
    result.Replace( wxT( "^{0}" ), wxT( "⁰" ) );
    result.Replace( wxT( "^{1}" ), wxT( "¹" ) );
    result.Replace( wxT( "^{2}" ), wxT( "²" ) );
    result.Replace( wxT( "^{3}" ), wxT( "³" ) );
    result.Replace( wxT( "^{4}" ), wxT( "⁴" ) );
    result.Replace( wxT( "^{5}" ), wxT( "⁵" ) );
    result.Replace( wxT( "^{6}" ), wxT( "⁶" ) );
    result.Replace( wxT( "^{7}" ), wxT( "⁷" ) );
    result.Replace( wxT( "^{8}" ), wxT( "⁸" ) );
    result.Replace( wxT( "^{9}" ), wxT( "⁹" ) );
    result.Replace( wxT( "^{n}" ), wxT( "ⁿ" ) );
    result.Replace( wxT( "^{-1}" ), wxT( "⁻¹" ) );
    result.Replace( wxT( "^{-2}" ), wxT( "⁻²" ) );
    // Without braces
    result.Replace( wxT( "^0" ), wxT( "⁰" ) );
    result.Replace( wxT( "^1" ), wxT( "¹" ) );
    result.Replace( wxT( "^2" ), wxT( "²" ) );
    result.Replace( wxT( "^3" ), wxT( "³" ) );
    result.Replace( wxT( "^4" ), wxT( "⁴" ) );
    result.Replace( wxT( "^5" ), wxT( "⁵" ) );
    result.Replace( wxT( "^6" ), wxT( "⁶" ) );
    result.Replace( wxT( "^7" ), wxT( "⁷" ) );
    result.Replace( wxT( "^8" ), wxT( "⁸" ) );
    result.Replace( wxT( "^9" ), wxT( "⁹" ) );
    result.Replace( wxT( "^n" ), wxT( "ⁿ" ) );
    
    // Subscripts: _{0} -> ₀ etc
    result.Replace( wxT( "_{0}" ), wxT( "₀" ) );
    result.Replace( wxT( "_{1}" ), wxT( "₁" ) );
    result.Replace( wxT( "_{2}" ), wxT( "₂" ) );
    result.Replace( wxT( "_{3}" ), wxT( "₃" ) );
    result.Replace( wxT( "_{4}" ), wxT( "₄" ) );
    result.Replace( wxT( "_{5}" ), wxT( "₅" ) );
    result.Replace( wxT( "_{6}" ), wxT( "₆" ) );
    result.Replace( wxT( "_{7}" ), wxT( "₇" ) );
    result.Replace( wxT( "_{8}" ), wxT( "₈" ) );
    result.Replace( wxT( "_{9}" ), wxT( "₉" ) );
    result.Replace( wxT( "_{n}" ), wxT( "ₙ" ) );
    result.Replace( wxT( "_{i}" ), wxT( "ᵢ" ) );
    result.Replace( wxT( "_{j}" ), wxT( "ⱼ" ) );
    // Without braces
    result.Replace( wxT( "_0" ), wxT( "₀" ) );
    result.Replace( wxT( "_1" ), wxT( "₁" ) );
    result.Replace( wxT( "_2" ), wxT( "₂" ) );
    result.Replace( wxT( "_3" ), wxT( "₃" ) );
    result.Replace( wxT( "_4" ), wxT( "₄" ) );
    result.Replace( wxT( "_5" ), wxT( "₅" ) );
    result.Replace( wxT( "_6" ), wxT( "₆" ) );
    result.Replace( wxT( "_7" ), wxT( "₇" ) );
    result.Replace( wxT( "_8" ), wxT( "₈" ) );
    result.Replace( wxT( "_9" ), wxT( "₉" ) );
    
    // Common electrical/physics symbols
    result.Replace( wxT( "\\ohm" ), wxT( "Ω" ) );
    result.Replace( wxT( "\\degree" ), wxT( "°" ) );
    result.Replace( wxT( "\\deg" ), wxT( "°" ) );
    
    return result;
}


// Helper function to escape underscores in markdown to prevent them from being converted to italics
// Preserves underscores inside code blocks (```...```) and inline code (`...`)
static wxString EscapeUnderscoresInMarkdown( const wxString& aMarkdown )
{
    wxString result = aMarkdown;
    
    // Track whether we're inside a code block or inline code
    bool inCodeBlock = false;
    bool inInlineCode = false;
    size_t pos = 0;
    
    while( pos < result.Length() )
    {
        wxChar ch = result[pos];
        
        // Check for code block markers (```)
        if( pos + 2 < result.Length() && result[pos] == wxT( '`' ) && 
            result[pos + 1] == wxT( '`' ) && result[pos + 2] == wxT( '`' ) )
        {
            inCodeBlock = !inCodeBlock;
            pos += 3;
            continue;
        }
        
        // Check for inline code markers (`) - but not if we're in a code block
        if( !inCodeBlock && ch == wxT( '`' ) )
        {
            // Check if this is a single backtick (not part of ```)
            // A single backtick is one where the previous char is not a backtick AND
            // the next char is not a backtick (or we're at start/end)
            bool isPrevBacktick = ( pos > 0 && result[pos - 1] == wxT( '`' ) );
            bool isNextBacktick = ( pos + 1 < result.Length() && result[pos + 1] == wxT( '`' ) );
            
            if( !isPrevBacktick && !isNextBacktick )
            {
                inInlineCode = !inInlineCode;
            }
            pos++;
            continue;
        }
        
        // Escape underscores that are not in code blocks or inline code
        if( !inCodeBlock && !inInlineCode && ch == wxT( '_' ) )
        {
            // Don't escape if already escaped
            if( pos == 0 || result[pos - 1] != wxT( '\\' ) )
            {
                result.insert( pos, wxT( "\\" ) );
                pos += 2; // Skip the backslash and underscore
                continue;
            }
        }
        
        pos++;
    }
    
    return result;
}


// Helper function to convert markdown to HTML with CSS styling
static wxString ConvertMarkdownToStyledHtml( const wxString& aMarkdown )
{
    // First convert any LaTeX notation to Unicode
    wxString processedMarkdown = ConvertLatexToUnicode( aMarkdown );
    
    // Escape underscores to prevent them from being converted to italics
    processedMarkdown = EscapeUnderscoresInMarkdown( processedMarkdown );
    
    wxString html;
    ConvertMarkdown2Html( processedMarkdown, html );
    
    // Wrap HTML with CSS styling for consistent font rendering
    // Use Apple system font stack for macOS, fallback to system defaults on other platforms
    wxString styledHtml = wxT( "<style>\n" );
    // Base font: Apple system font on macOS, system default on other platforms
    styledHtml += wxT( "body, html, p, div, span, li, ul, ol, h1, h2, h3, h4, h5, h6, blockquote { font-family: -apple-system, BlinkMacSystemFont, \"SF Pro Text\", \"SF Pro Display\", \"Helvetica Neue\", Helvetica, Arial, sans-serif; }\n" );
    // Headers: bold, not italic
    styledHtml += wxT( "h1, h2, h3, h4, h5, h6 { font-weight: bold; font-style: normal; margin: 8px 0 4px 0; }\n" );
    styledHtml += wxT( "h1 { font-size: 1.4em; } h2 { font-size: 1.3em; } h3 { font-size: 1.2em; } h4, h5, h6 { font-size: 1.1em; }\n" );
    // Code blocks use monospace font
    styledHtml += wxT( "code { background-color: rgba(128, 128, 128, 0.15); padding: 2px 4px; border-radius: 3px; font-family: \"SF Mono\", Monaco, \"Cascadia Code\", \"Roboto Mono\", Consolas, \"Courier New\", monospace; }\n" );
    styledHtml += wxT( "pre { background-color: rgba(128, 128, 128, 0.15); padding: 8px; border-radius: 4px; overflow-x: auto; font-family: \"SF Mono\", Monaco, \"Cascadia Code\", \"Roboto Mono\", Consolas, \"Courier New\", monospace; white-space: pre-wrap; }\n" );
    styledHtml += wxT( "pre code { background-color: transparent; padding: 0; }\n" );
    // Table styling
    styledHtml += wxT( "table { border-collapse: collapse; width: 100%; margin: 8px 0; font-size: 0.9em; }\n" );
    styledHtml += wxT( "th, td { border: 1px solid rgba(128, 128, 128, 0.4); padding: 6px 10px; text-align: left; }\n" );
    styledHtml += wxT( "th { background-color: rgba(128, 128, 128, 0.15); font-weight: bold; }\n" );
    styledHtml += wxT( "tr:nth-child(even) { background-color: rgba(128, 128, 128, 0.05); }\n" );
    styledHtml += wxT( "</style>\n" );
    styledHtml += html;
    
    return styledHtml;
}


/**
 * Custom HTML window for chat messages that uses transparent/parent background.
 * Overrides SetPage to use the parent's background color instead of wxSYS_COLOUR_WINDOW.
 */
class MESSAGE_HTML_WINDOW : public HTML_WINDOW
{
public:
    MESSAGE_HTML_WINDOW( wxWindow* aParent, wxWindowID aId = wxID_ANY,
                         const wxPoint& aPos = wxDefaultPosition,
                         const wxSize& aSize = wxDefaultSize,
                         long aStyle = wxHW_DEFAULT_STYLE ) :
            HTML_WINDOW( aParent, aId, aPos, aSize, aStyle )
    {
        // Set window background to transparent so it doesn't paint over parent
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        // Try to get the actual scroll window background (CHAT_MESSAGE_PANEL)
        wxColour parentBg = GetScrollWindowBackground();
        SetBackgroundColour( parentBg );
        
#ifdef __WXMSW__
        // Windows: Enable ClearType and proper font rendering for better text quality
        // Use Segoe UI (Windows 7+) for consistent, modern appearance
        wxFont font = GetFont();
        if( !font.IsOk() )
            font = *wxNORMAL_FONT;
        
        // Set to Segoe UI if available, which has excellent ClearType rendering
        font.SetFaceName( wxT("Segoe UI") );
        if( !font.IsOk() ) // Fallback if Segoe UI not available
            font = wxFont( wxNORMAL_FONT->GetPointSize(), 
                          wxFONTFAMILY_DEFAULT, 
                          wxFONTSTYLE_NORMAL, 
                          wxFONTWEIGHT_NORMAL );
        SetFont( font );
        
        // Enable double buffering for smoother rendering on Windows
        SetDoubleBuffered( true );
#endif
        
        // Bind to theme change event to update colors dynamically
        Bind( wxEVT_SYS_COLOUR_CHANGED, &MESSAGE_HTML_WINDOW::onMsgThemeChanged, this );
    }
    
    ~MESSAGE_HTML_WINDOW()
    {
        Unbind( wxEVT_SYS_COLOUR_CHANGED, &MESSAGE_HTML_WINDOW::onMsgThemeChanged, this );
    }
    
    bool SetPage( const wxString& aSource ) override
    {
        m_msgPageSource = aSource;
        return RenderPage();
    }
    
    /**
     * Handle link clicks - open URLs in the default browser.
     */
    void OnLinkClicked( const wxHtmlLinkInfo& aLink ) override
    {
        wxString href = aLink.GetHref();
        
        // Open external URLs in the default browser
        if( href.StartsWith( wxT( "http://" ) ) || href.StartsWith( wxT( "https://" ) ) )
        {
            wxLaunchDefaultBrowser( href );
        }
        else
        {
            // For non-http links, use default behavior
            wxHtmlWindow::OnLinkClicked( aLink );
        }
    }
    
private:
    wxString m_msgPageSource;
    
    /**
     * Render the page with current theme colors.
     */
    bool RenderPage()
    {
        // Get the scroll window's background color (the actual chat panel)
        wxColour bgColor = GetScrollWindowBackground();
        
        // Set text color based on theme: black for light theme, white for dark theme
        bool isDark = KIPLATFORM::UI::IsDarkTheme();
        wxColour fgColor = isDark ? wxColour( 255, 255, 255 ) : wxColour( 0, 0, 0 );
        wxColour linkColor = wxSystemSettings::GetColour( wxSYS_COLOUR_HOTLIGHT );
        
#ifdef __WXMSW__
        // Windows: Add enhanced CSS styling for better text rendering with ClearType
        wxString html = wxString::Format( 
            wxT( "<html>\n<head>\n"
                 "<style type='text/css'>\n"
                 "body { font-family: 'Segoe UI', 'Tahoma', sans-serif; "
                 "       font-size: 10pt; "
                 "       line-height: 1.5; "
                 "       text-rendering: optimizeLegibility; "
                 "       -webkit-font-smoothing: antialiased; }\n"
                 "h1, h2, h3, h4, h5, h6 { font-weight: bold; font-style: normal; margin: 8px 0 4px 0; }\n"
                 "h1 { font-size: 1.4em; } h2 { font-size: 1.3em; } h3 { font-size: 1.2em; }\n"
                 "code { font-family: 'Consolas', 'Courier New', monospace; "
                 "       background-color: %s; "
                 "       padding: 2px 4px; "
                 "       border-radius: 3px; }\n"
                 "pre { font-family: 'Consolas', 'Courier New', monospace; "
                 "      background-color: %s; "
                 "      padding: 8px; "
                 "      border-radius: 5px; "
                 "      overflow-x: auto; }\n"
                 "table { border-collapse: collapse; width: 100%%; margin: 8px 0; font-size: 0.9em; }\n"
                 "th, td { border: 1px solid %s; padding: 6px 10px; text-align: left; }\n"
                 "th { background-color: %s; font-weight: bold; }\n"
                 "</style>\n</head>\n"
                 "<body text='%s' bgcolor='transparent' link='%s' style='background-color: transparent;'>\n" ),
            isDark ? wxT("#2d2d2d") : wxT("#f5f5f5"),  // Code background
            isDark ? wxT("#2d2d2d") : wxT("#f5f5f5"),  // Pre background
            isDark ? wxT("#555555") : wxT("#cccccc"),  // Table border
            isDark ? wxT("#2d2d2d") : wxT("#f5f5f5"),  // Table header background
            fgColor.GetAsString( wxC2S_HTML_SYNTAX ),
            linkColor.GetAsString( wxC2S_HTML_SYNTAX ) );
#else
        // Mac/Linux: Use simpler HTML (existing behavior)
        wxString html = wxString::Format( wxT( "<html>\n<body text='%s' bgcolor='transparent' link='%s' style='background-color: transparent;'>\n" ),
                                          fgColor.GetAsString( wxC2S_HTML_SYNTAX ),
                                          linkColor.GetAsString( wxC2S_HTML_SYNTAX ) );
#endif
        html.Append( m_msgPageSource );
        html.Append( wxT( "\n</body>\n</html>" ) );
        
        // Call base wxHtmlWindow::SetPage directly to avoid HTML_WINDOW's wrapper
        bool result = wxHtmlWindow::SetPage( html );
        
        // Update window background after setting page
        SetBackgroundColour( bgColor );
        Refresh();
        
        return result;
    }
    
    /**
     * Handle theme change event - re-render with new theme colors.
     */
    void onMsgThemeChanged( wxSysColourChangedEvent& aEvent )
    {
        // Update background color for new theme
        wxColour bgColor = GetScrollWindowBackground();
        SetBackgroundColour( bgColor );
        
        // Re-render page with new theme colors if we have content
        if( !m_msgPageSource.IsEmpty() )
        {
            RenderPage();
        }
        
        // Don't call Skip() - we handle this completely ourselves
        // The base class HTML_WINDOW will also handle this event but with empty/wrong source
    }
    
    // Helper to find the CHAT_MESSAGE_PANEL (scroll window) background
    wxColour GetScrollWindowBackground()
    {
#ifdef __WXMSW__
        // Windows: Use consistent background color based on dark mode
        bool isDark = KIPLATFORM::UI::IsDarkTheme();
        if( isDark )
        {
            return wxColour( 30, 30, 30 );  // Dark mode background
        }
        else
        {
            return wxColour( 255, 255, 255 );  // Light mode background
        }
#else
        // Mac/Linux: Try to find the scroll window and get its background
        wxWindow* parent = GetParent();
        int level = 0;
        while( parent && level < 10 )
        {
            // Try to find CHAT_MESSAGE_PANEL (wxScrolledWindow)
            wxScrolledWindow* scrolled = dynamic_cast<wxScrolledWindow*>( parent );
            if( scrolled )
            {
                wxColour bg = scrolled->GetBackgroundColour();
                if( bg.IsOk() && bg != wxNullColour )
                {
                    return bg;
                }
            }
            
            parent = parent->GetParent();
            level++;
        }
        
        // Fallback
        return wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
#endif
    }
};


// Message bubble implementation
CHAT_MESSAGE_BUBBLE::CHAT_MESSAGE_BUBBLE( wxWindow* aParent, bool aIsUser, const wxString& aMessage, bool aIsHtml ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_isUser( aIsUser ),
        m_message( aMessage ),
        m_isHtml( aIsHtml ),
        m_expanded( false ),
        m_htmlWindow( nullptr ),
        m_textLabel( nullptr ),
        m_expandButton( nullptr ),
        m_sizer( nullptr )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    
#ifdef __WXMSW__
    // Windows: Use proper background matching the panel
    bool isDark = KIPLATFORM::UI::IsDarkTheme();
    if( isDark )
    {
        SetBackgroundColour( wxColour( 30, 30, 30 ) );  // Match panel background
    }
    else
    {
        SetBackgroundColour( wxColour( 255, 255, 255 ) );  // Match panel background
    }
#else
    // Mac/Linux: Use system window background
    SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );
#endif

    m_sizer = new wxBoxSizer( wxVERTICAL );

    // For AI messages, always convert markdown to HTML and use MESSAGE_HTML_WINDOW
    // For user messages, use plain text with wxStaticText
    if( !m_isUser )
    {
        // AI messages: convert markdown to HTML
        wxString displayText = needsTruncation() ? getTruncatedText() : m_message;
        wxString htmlContent = aIsHtml ? displayText : ConvertMarkdownToStyledHtml( displayText );
        
        m_htmlWindow = new MESSAGE_HTML_WINDOW( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                wxHW_SCROLLBAR_NEVER | wxBORDER_NONE );
        m_htmlWindow->SetPage( htmlContent );
        m_sizer->Add( m_htmlWindow, 0, wxEXPAND | wxTOP | wxBOTTOM, 8 );
        m_isHtml = true; // Mark as HTML since we're using HTML_WINDOW
        
        // Forward mouse wheel events to parent for scrolling
        m_htmlWindow->Bind( wxEVT_MOUSEWHEEL, [this]( wxMouseEvent& evt ) {
            wxWindow* parent = GetParent();
            if( parent )
            {
                wxMouseEvent newEvt( evt );
                newEvt.SetEventObject( parent );
                parent->GetEventHandler()->ProcessEvent( newEvt );
            }
        });
    }
    else if( m_isHtml )
    {
        // User messages that are explicitly HTML
        m_htmlWindow = new HTML_WINDOW( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                        wxHW_SCROLLBAR_NEVER | wxBORDER_NONE );
        m_htmlWindow->SetPage( needsTruncation() ? getTruncatedText() : m_message );
        m_sizer->Add( m_htmlWindow, 0, wxEXPAND | wxALL, 8 );
    }
    else
    {
        // User messages: plain text
        wxString displayText = needsTruncation() ? getTruncatedText() : m_message;
        m_textLabel = new wxStaticText( this, wxID_ANY, displayText, wxDefaultPosition, wxDefaultSize, 0 );
        
#ifdef __WXMSW__
        // Windows: Use Segoe UI for better text rendering and professional appearance
        wxFont userFont = m_textLabel->GetFont();
        if( !userFont.IsOk() )
            userFont = *wxNORMAL_FONT;
        
        userFont.SetFaceName( wxT("Segoe UI") );
        userFont.SetPointSize( userFont.GetPointSize() );  // Keep default size
        if( userFont.IsOk() )
            m_textLabel->SetFont( userFont );
        
        // Set text color for dark mode visibility
        bool isDark = KIPLATFORM::UI::IsDarkTheme();
        if( isDark )
        {
            m_textLabel->SetForegroundColour( wxColour( 255, 255, 255 ) );  // White text for dark mode
            m_textLabel->SetBackgroundColour( wxColour( 58, 58, 60 ) );  // Match bubble bgColor
        }
        else
        {
            m_textLabel->SetForegroundColour( wxColour( 0, 0, 0 ) );  // Black text for light mode
            m_textLabel->SetBackgroundColour( wxColour( 242, 242, 247 ) );  // Match bubble bgColor
        }
        
        // Add more padding for Windows for cleaner look
        m_sizer->Add( m_textLabel, 0, wxEXPAND | wxALL, 12 );
#else
        // Mac/Linux: Keep existing styling
        m_sizer->Add( m_textLabel, 0, wxEXPAND | wxALL, 8 );
#endif
    }

    // Add "See more" button for long AI messages (right-aligned using horizontal sizer)
    if( !m_isUser && needsTruncation() )
    {
        m_expandButton =
                new wxButton( this, wxID_ANY, wxT( "See more v" ), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE );
        m_expandButton->SetBackgroundColour( GetBackgroundColour() );
        
#ifdef __WXMSW__
        // Windows: Use accent color for link-like appearance, with better hover styling
        m_expandButton->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_HOTLIGHT ) );
        
        // Set Segoe UI font for consistency
        wxFont btnFont = m_expandButton->GetFont();
        if( !btnFont.IsOk() )
            btnFont = *wxNORMAL_FONT;
        btnFont.SetFaceName( wxT("Segoe UI") );
        btnFont.SetUnderlined( true );  // Underline to indicate it's clickable
        if( btnFont.IsOk() )
            m_expandButton->SetFont( btnFont );
#else
        // Mac/Linux: Keep existing styling
        m_expandButton->SetForegroundColour( wxColour( 0, 120, 200 ) ); // Link-like blue
#endif
        
        m_expandButton->SetCursor( wxCursor( wxCURSOR_HAND ) );

        // Add button directly with right alignment - no stretch spacer to prevent bubble expansion
        m_sizer->Add( m_expandButton, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 8 );

        m_expandButton->Bind( wxEVT_BUTTON, &CHAT_MESSAGE_BUBBLE::onExpandClick, this );
    }

    SetSizer( m_sizer );
    // Initial layout - will be updated when size is set
    Layout();

    Bind( wxEVT_PAINT, &CHAT_MESSAGE_BUBBLE::onPaint, this );
    Bind( wxEVT_SIZE, &CHAT_MESSAGE_BUBBLE::onSize, this );
}


void CHAT_MESSAGE_BUBBLE::SetMessage( const wxString& aMessage, bool aIsHtml )
{
    m_message = aMessage;
    
    // Determine what text to display (truncated or full)
    wxString displayText = ( m_expanded || !needsTruncation() ) ? m_message : getTruncatedText();

    // For AI messages, always convert markdown to HTML and use MESSAGE_HTML_WINDOW
    if( !m_isUser )
    {
        wxString htmlContent = aIsHtml ? displayText : ConvertMarkdownToStyledHtml( displayText );
        m_isHtml = true; // Mark as HTML since we're using HTML_WINDOW
        
        if( !m_htmlWindow )
        {
            if( m_textLabel )
            {
                m_sizer->Detach( m_textLabel );
                m_textLabel->Destroy();
                m_textLabel = nullptr;
            }
            m_htmlWindow = new MESSAGE_HTML_WINDOW( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                    wxHW_SCROLLBAR_NEVER | wxBORDER_NONE );
            m_sizer->Add( m_htmlWindow, 0, wxEXPAND | wxTOP | wxBOTTOM, 8 );
            
            // Forward mouse wheel events to parent for scrolling
            m_htmlWindow->Bind( wxEVT_MOUSEWHEEL, [this]( wxMouseEvent& evt ) {
                wxWindow* parent = GetParent();
                if( parent )
                {
                    wxMouseEvent newEvt( evt );
                    newEvt.SetEventObject( parent );
                    parent->GetEventHandler()->ProcessEvent( newEvt );
                }
            });
        }
        m_htmlWindow->SetPage( htmlContent );
    }
    else
    {
        // User messages: use plain text or HTML based on aIsHtml parameter
        m_isHtml = aIsHtml;
        
        if( m_isHtml )
        {
            if( !m_htmlWindow )
            {
                if( m_textLabel )
                {
                    m_sizer->Detach( m_textLabel );
                    m_textLabel->Destroy();
                    m_textLabel = nullptr;
                }
                m_htmlWindow = new HTML_WINDOW( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                wxHW_SCROLLBAR_NEVER | wxBORDER_NONE );
                m_sizer->Add( m_htmlWindow, 0, wxEXPAND | wxALL, 8 );
            }
            m_htmlWindow->SetPage( displayText );
        }
        else
        {
            if( !m_textLabel )
            {
                if( m_htmlWindow )
                {
                    m_sizer->Detach( m_htmlWindow );
                    m_htmlWindow->Destroy();
                    m_htmlWindow = nullptr;
                }
                m_textLabel = new wxStaticText( this, wxID_ANY, displayText, wxDefaultPosition, wxDefaultSize, 0 );
#ifdef __WXMSW__
                // Windows: Set background color to match bubble to prevent white patches
                bool isDark = KIPLATFORM::UI::IsDarkTheme();
                if( isDark )
                {
                    m_textLabel->SetBackgroundColour( wxColour( 58, 58, 60 ) );  // Match bubble bgColor
                }
                else
                {
                    m_textLabel->SetBackgroundColour( wxColour( 242, 242, 247 ) );  // Match bubble bgColor
                }
                m_sizer->Add( m_textLabel, 0, wxEXPAND | wxALL, 12 );
#else
                m_sizer->Add( m_textLabel, 0, wxEXPAND | wxALL, 8 );
#endif
            }
            else
            {
                m_textLabel->SetLabel( displayText );
            }
        }
    }

    // Add or remove expand button based on message length (right-aligned using horizontal sizer)
    if( !m_isUser && needsTruncation() && !m_expandButton )
    {
        m_expandButton = new wxButton( this, wxID_ANY, m_expanded ? wxT( "See less ^" ) : wxT( "See more v" ),
                                       wxDefaultPosition, wxDefaultSize, wxBORDER_NONE );
        m_expandButton->SetBackgroundColour( GetBackgroundColour() );
        m_expandButton->SetForegroundColour( wxColour( 0, 120, 200 ) );
        m_expandButton->SetCursor( wxCursor( wxCURSOR_HAND ) );

        // Add button directly with right alignment - no stretch spacer to prevent bubble expansion
        m_sizer->Add( m_expandButton, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 8 );

        m_expandButton->Bind( wxEVT_BUTTON, &CHAT_MESSAGE_BUBBLE::onExpandClick, this );
    }
    else if( m_expandButton && !needsTruncation() )
    {
        // Message is now short enough - remove expand button
        m_sizer->Detach( m_expandButton );
        m_expandButton->Destroy();
        m_expandButton = nullptr;
    }

    updateLayout();
}


void CHAT_MESSAGE_BUBBLE::AppendText( const wxString& aText )
{
    // Smart concatenation: add space if needed to prevent words running together
    // e.g., "Hello:Now" should become "Hello: Now"
    if( !m_message.IsEmpty() && !aText.IsEmpty() )
    {
        wxChar lastChar = m_message.Last();
        wxChar firstChar = aText[0];

        // Add space if: previous ends with punctuation (not whitespace) AND next starts with letter
        bool needsSpace =
                ( lastChar == ':' || lastChar == '.' || lastChar == '!' || lastChar == '?' ) && wxIsalpha( firstChar );

        if( needsSpace )
        {
            m_message += wxT( " " );
        }
    }

    m_message += aText;

    // During streaming, always show full text (expanded mode)
    // After streaming completes, it will be truncated if needed
    if( !m_isUser )
    {
        // AI messages: ensure we have MESSAGE_HTML_WINDOW and convert markdown to HTML
        if( !m_htmlWindow )
        {
            if( m_textLabel )
            {
                m_sizer->Detach( m_textLabel );
                m_textLabel->Destroy();
                m_textLabel = nullptr;
            }
            m_htmlWindow = new MESSAGE_HTML_WINDOW( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                    wxHW_SCROLLBAR_NEVER | wxBORDER_NONE );
            m_sizer->Add( m_htmlWindow, 0, wxEXPAND | wxTOP | wxBOTTOM, 8 );
            m_isHtml = true;
            
            // Forward mouse wheel events to parent for scrolling
            m_htmlWindow->Bind( wxEVT_MOUSEWHEEL, [this]( wxMouseEvent& evt ) {
                wxWindow* parent = GetParent();
                if( parent )
                {
                    wxMouseEvent newEvt( evt );
                    newEvt.SetEventObject( parent );
                    parent->GetEventHandler()->ProcessEvent( newEvt );
                }
            });
        }
        // Convert markdown to HTML for display during streaming
        wxString htmlContent = ConvertMarkdownToStyledHtml( m_message );
        m_htmlWindow->SetPage( htmlContent );
    }
    else if( m_isHtml && m_htmlWindow )
    {
        // User messages that are HTML
        m_htmlWindow->SetPage( m_message );
    }
    else if( m_textLabel )
    {
        // User messages: plain text
        m_textLabel->SetLabel( m_message );
    }

    // Check if we need to add the expand button (message just became long enough, right-aligned)
    if( !m_isUser && !m_expandButton && needsTruncation() )
    {
        // During streaming, we're expanded (showing full text) so label should be "See less"
        m_expanded = true;
        m_expandButton =
                new wxButton( this, wxID_ANY, wxT( "See less ^" ), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE );
        m_expandButton->SetBackgroundColour( GetBackgroundColour() );
        m_expandButton->SetForegroundColour( wxColour( 0, 120, 200 ) );
        m_expandButton->SetCursor( wxCursor( wxCURSOR_HAND ) );

        // Add button directly with right alignment - no stretch spacer to prevent bubble expansion
        m_sizer->Add( m_expandButton, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 8 );

        m_expandButton->Bind( wxEVT_BUTTON, &CHAT_MESSAGE_BUBBLE::onExpandClick, this );
    }

    updateLayout();
}


bool CHAT_MESSAGE_BUBBLE::needsTruncation() const
{
    // Only AI messages can be truncated
    if( m_isUser )
        return false;

    return m_message.Length() > TRUNCATE_THRESHOLD;
}


wxString CHAT_MESSAGE_BUBBLE::getTruncatedText() const
{
    if( !needsTruncation() )
        return m_message;

    // Truncate at threshold, try to find a word boundary
    wxString truncated = m_message.Left( TRUNCATE_THRESHOLD );

    // Try to break at last space to avoid cutting words
    int lastSpace = truncated.Find( wxT( ' ' ), true ); // Find from end
    if( lastSpace > TRUNCATE_THRESHOLD * 0.7 )          // Only if we don't lose too much
    {
        truncated = truncated.Left( lastSpace );
    }

    return truncated + wxT( "..." );
}


void CHAT_MESSAGE_BUBBLE::updateDisplayedText()
{
    wxString displayText = ( m_expanded || !needsTruncation() ) ? m_message : getTruncatedText();

    // For AI messages, always convert markdown to HTML
    if( !m_isUser )
    {
        if( !m_htmlWindow )
        {
            if( m_textLabel )
            {
                m_sizer->Detach( m_textLabel );
                m_textLabel->Destroy();
                m_textLabel = nullptr;
            }
            m_htmlWindow = new MESSAGE_HTML_WINDOW( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                    wxHW_SCROLLBAR_NEVER | wxBORDER_NONE );
            m_sizer->Add( m_htmlWindow, 0, wxEXPAND | wxTOP | wxBOTTOM, 8 );
            m_isHtml = true;
            
            // Forward mouse wheel events to parent for scrolling
            m_htmlWindow->Bind( wxEVT_MOUSEWHEEL, [this]( wxMouseEvent& evt ) {
                wxWindow* parent = GetParent();
                if( parent )
                {
                    wxMouseEvent newEvt( evt );
                    newEvt.SetEventObject( parent );
                    parent->GetEventHandler()->ProcessEvent( newEvt );
                }
            });
        }
        wxString htmlContent = ConvertMarkdownToStyledHtml( displayText );
        m_htmlWindow->SetPage( htmlContent );
    }
    else if( m_isHtml && m_htmlWindow )
    {
        // User messages that are HTML
        m_htmlWindow->SetPage( displayText );
    }
    else if( m_textLabel )
    {
        // User messages: plain text
        m_textLabel->SetLabel( displayText );
    }

    // Update button label
    if( m_expandButton )
    {
        if( m_expanded )
            m_expandButton->SetLabel( wxT( "See less ^" ) );
        else
            m_expandButton->SetLabel( wxT( "See more v" ) );
    }
}


void CHAT_MESSAGE_BUBBLE::ToggleExpand()
{
    SetExpanded( !m_expanded );
}


void CHAT_MESSAGE_BUBBLE::SetExpanded( bool aExpanded )
{
    if( m_expanded != aExpanded )
    {
        m_expanded = aExpanded;
        updateDisplayedText();
        updateLayout();

        // Notify parent to update layout
        wxWindow* parent = GetParent();
        if( parent )
        {
            parent->Layout();
            parent->Refresh();

            // If parent is a scrolled window, update virtual size
            wxScrolledWindow* scrolled = dynamic_cast<wxScrolledWindow*>( parent );
            if( scrolled )
            {
                scrolled->FitInside();
            }
        }
    }
}


void CHAT_MESSAGE_BUBBLE::onExpandClick( wxCommandEvent& aEvent )
{
    ToggleExpand();
}


void CHAT_MESSAGE_BUBBLE::onPaint( wxPaintEvent& aEvent )
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

    // Get theme-aware colors
    bool     isDark = KIPLATFORM::UI::IsDarkTheme();
    wxColour bgColor;
    wxColour borderColor;

    // Logic swapped/modified:
    // User messages now use the "AI" theme (neutral/grey bubble)
    // AI messages now have no bubble (transparent)

    if( m_isUser )
    {
        // User messages: use the neutral bubble style (formerly AI style)
        wxColour baseColor = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE );
        
#ifdef __WXMSW__
        // Windows: Use more professional colors - subtle grey bubble
        if( isDark )
        {
            bgColor = wxColour( 58, 58, 60 );      // Dark grey, modern
            borderColor = wxColour( 72, 72, 74 );  // Slightly lighter border
        }
        else
        {
            bgColor = wxColour( 242, 242, 247 );   // Light grey, iOS-like
            borderColor = wxColour( 229, 229, 234 ); // Subtle border
        }
#else
        // Mac/Linux: Keep existing color logic
        if( isDark )
        {
            bgColor = baseColor.ChangeLightness( 110 );
            borderColor = baseColor.ChangeLightness( 120 );
        }
        else
        {
            bgColor = baseColor.ChangeLightness( 95 );
            borderColor = baseColor.ChangeLightness( 90 );
        }
#endif

#ifdef __WXMSW__
        // Windows: Use anti-aliasing and wxGraphicsContext for smoother rendering
        wxGraphicsContext* gc = wxGraphicsContext::Create( dc );
        if( gc )
        {
            // Enable high-quality anti-aliasing
            gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );
            
            // Set colors with proper alpha blending
            gc->SetBrush( wxBrush( bgColor ) );
            gc->SetPen( wxPen( borderColor, 1 ) );
            
            // Draw rounded rectangle with smooth edges (larger radius for modern look)
            wxGraphicsPath path = gc->CreatePath();
            path.AddRoundedRectangle( rect.x, rect.y, rect.width, rect.height, 12 );
            gc->FillPath( path );
            gc->StrokePath( path );
            
            delete gc;
        }
        else
        {
            // Fallback to regular drawing if wxGraphicsContext unavailable
            dc.SetBrush( wxBrush( bgColor ) );
            dc.SetPen( wxPen( borderColor, 1 ) );
            dc.DrawRoundedRectangle( rect, 12 );
        }
#else
        // Mac/Linux: Use existing drawing (works well there)
        dc.SetBrush( wxBrush( bgColor ) );
        dc.SetPen( wxPen( borderColor, 1 ) );
        dc.DrawRoundedRectangle( rect, 10 );
#endif
    }
    else
    {
        // AI messages: transparent (no border, no background color)
#ifdef __WXMSW__
        // Windows: Match the panel background for clean appearance
        bool isDark = KIPLATFORM::UI::IsDarkTheme();
        if( isDark )
        {
            dc.SetBrush( wxBrush( wxColour( 30, 30, 30 ) ) );  // Match panel
        }
        else
        {
            dc.SetBrush( wxBrush( wxColour( 255, 255, 255 ) ) );  // Match panel
        }
        dc.SetPen( *wxTRANSPARENT_PEN );
#else
        // Mac/Linux: Keep transparent
        dc.SetBrush( *wxTRANSPARENT_BRUSH );
        dc.SetPen( *wxTRANSPARENT_PEN );
#endif
        dc.DrawRoundedRectangle( rect, 10 );
    }

    aEvent.Skip();
}


void CHAT_MESSAGE_BUBBLE::onSize( wxSizeEvent& aEvent )
{
    updateLayout();
    Refresh();
    aEvent.Skip();
}


void CHAT_MESSAGE_BUBBLE::updateLayout()
{
    // Use max size if set, as that represents the true available width from the parent
    int availableWidth = GetMaxSize().GetWidth();

    if( availableWidth <= 0 )
        availableWidth = GetClientSize().GetWidth();

    if( availableWidth <= 0 )
        availableWidth = 400;

    int contentWidth = availableWidth - 16; // Account for padding
    if( contentWidth < 200 )
        contentWidth = 200;

    if( m_htmlWindow )
    {
        // Set the HTML window width and let it calculate height based on content
        m_htmlWindow->SetSize( contentWidth, -1 );
        
        // Get the internal representation to calculate content height
        wxHtmlContainerCell* cell = m_htmlWindow->GetInternalRepresentation();
        if( cell )
        {
            cell->Layout( contentWidth );
            
            int contentHeight = cell->GetHeight();
            // Add some padding
            m_htmlWindow->SetMinSize( wxSize( contentWidth, contentHeight + 4 ) );
            m_htmlWindow->SetSize( contentWidth, contentHeight + 4 );
        }
        
        m_htmlWindow->InvalidateBestSize();
        InvalidateBestSize();
    }
    else if( m_textLabel )
    {
        int wrapWidth = contentWidth;
        if( wrapWidth > 0 )
            m_textLabel->Wrap( wrapWidth );
        else
            m_textLabel->Wrap( 200 );

        m_textLabel->InvalidateBestSize();
        InvalidateBestSize();
    }

    Layout();
    Refresh();
}


// Queued message panel implementation
QUEUED_MESSAGE_PANEL::QUEUED_MESSAGE_PANEL( wxWindow* aParent, const wxString& aMessage,
                                            std::function<void()> aOnCancel ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_message( aMessage ),
        m_label( nullptr ),
        m_messageText( nullptr ),
        m_cancelButton( nullptr ),
        m_onCancel( aOnCancel )
{
    // Dark background like Cursor's queued message
    SetBackgroundColour( wxColour( 45, 45, 50 ) );

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );
    wxBoxSizer* headerSizer = new wxBoxSizer( wxHORIZONTAL );

    // "1 Queued" label
    m_label = new wxStaticText( this, wxID_ANY, wxT( "1 Queued" ) );
    m_label->SetForegroundColour( wxColour( 180, 180, 180 ) );
    wxFont labelFont = m_label->GetFont();
    labelFont.SetWeight( wxFONTWEIGHT_BOLD );
    labelFont.SetPointSize( labelFont.GetPointSize() - 1 );
    m_label->SetFont( labelFont );

    headerSizer->Add( m_label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 12 );

    // Cancel/delete button (×)
    m_cancelButton = new wxButton( this, wxID_ANY, wxT( "×" ), wxDefaultPosition, wxSize( 24, 24 ), wxBORDER_NONE );
    m_cancelButton->SetBackgroundColour( GetBackgroundColour() );
    m_cancelButton->SetForegroundColour( wxColour( 180, 180, 180 ) );
    m_cancelButton->SetCursor( wxCursor( wxCURSOR_HAND ) );
    m_cancelButton->SetToolTip( wxT( "Cancel queued message" ) );

    headerSizer->Add( m_cancelButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );

    mainSizer->Add( headerSizer, 0, wxEXPAND | wxTOP, 8 );

    // Message text (truncated if too long)
    wxString displayMsg = m_message;
    if( displayMsg.length() > 60 )
        displayMsg = displayMsg.Left( 57 ) + wxT( "..." );

    m_messageText = new wxStaticText( this, wxID_ANY, displayMsg );
    m_messageText->SetForegroundColour( wxColour( 220, 220, 220 ) );
    m_messageText->Wrap( 300 );

    mainSizer->Add( m_messageText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12 );

    SetSizer( mainSizer );

    m_cancelButton->Bind( wxEVT_BUTTON, &QUEUED_MESSAGE_PANEL::onCancelClick, this );
    Bind( wxEVT_PAINT, &QUEUED_MESSAGE_PANEL::onPaint, this );
}


void QUEUED_MESSAGE_PANEL::SetMessage( const wxString& aMessage )
{
    m_message = aMessage;
    wxString displayMsg = m_message;
    if( displayMsg.length() > 60 )
        displayMsg = displayMsg.Left( 57 ) + wxT( "..." );
    m_messageText->SetLabel( displayMsg );
    Layout();
}


void QUEUED_MESSAGE_PANEL::onPaint( wxPaintEvent& aEvent )
{
    wxPaintDC dc( this );
    wxSize    size = GetClientSize();

    // Draw rounded rectangle background
    dc.SetBrush( wxBrush( GetBackgroundColour() ) );
    dc.SetPen( wxPen( wxColour( 70, 70, 80 ), 1 ) );
    dc.DrawRoundedRectangle( 0, 0, size.GetWidth(), size.GetHeight(), 8 );
}


void QUEUED_MESSAGE_PANEL::onCancelClick( wxCommandEvent& aEvent )
{
    if( m_onCancel )
        m_onCancel();
}


// Chat message panel implementation
CHAT_MESSAGE_PANEL::CHAT_MESSAGE_PANEL( wxWindow* aParent, wxWindowID aId, const wxPoint& aPos, const wxSize& aSize,
                                        long aStyle ) :
        wxScrolledWindow( aParent, aId, aPos, aSize, aStyle ),
        m_queuedPanel( nullptr ),
        m_typingIndicator( nullptr ),
        m_statusIndicator( nullptr ),
        m_loadingSkeleton( nullptr ),
        m_mainSizer( nullptr ),
        m_messageSpacing( 2 ),
        m_horizontalPadding( 8 ),
        m_lastPanelWidth( 0 ),
        m_isDragging( false ),
        m_dragStartPos( 0, 0 ),
        m_dragStartScrollY( 0 )
{
#ifdef __WXMSW__
    // Windows: Use proper background color that adapts to dark mode
    bool isDark = KIPLATFORM::UI::IsDarkTheme();
    if( isDark )
    {
        // Dark mode: Use a dark grey background (like VS Code, Discord)
        SetBackgroundColour( wxColour( 30, 30, 30 ) );
    }
    else
    {
        // Light mode: Use a clean white background
        SetBackgroundColour( wxColour( 255, 255, 255 ) );
    }
    
    // Enable double buffering for smooth scrolling
    SetDoubleBuffered( true );
    
    // Increase spacing for cleaner Windows look
    m_messageSpacing = 12;      // More breathing room between messages
    m_horizontalPadding = 16;   // More side padding
#else
    // Mac/Linux: Keep original styling
    SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );
#endif
    
    SetScrollRate( 0, 10 );

    // Hide scrollbars completely for a cleaner look
    // ShowScrollbars( wxSHOW_SB_NEVER, wxSHOW_SB_NEVER ); // <- this is causing issues when scrolling

    m_mainSizer = new wxBoxSizer( wxVERTICAL );
    SetSizer( m_mainSizer );

    Bind( wxEVT_SIZE, &CHAT_MESSAGE_PANEL::onSize, this );
    
    // Bind mouse events for drag-to-scroll support
    Bind( wxEVT_LEFT_DOWN, &CHAT_MESSAGE_PANEL::onMouseDown, this );
    Bind( wxEVT_LEFT_UP, &CHAT_MESSAGE_PANEL::onMouseUp, this );
    Bind( wxEVT_MOTION, &CHAT_MESSAGE_PANEL::onMouseMove, this );
    Bind( wxEVT_MOUSE_CAPTURE_LOST, &CHAT_MESSAGE_PANEL::onMouseCaptureLost, this );
    
#ifdef __WXMSW__
    // Windows: Listen for theme changes to update background
    Bind( wxEVT_SYS_COLOUR_CHANGED, &CHAT_MESSAGE_PANEL::onThemeChanged, this );
#endif
}


void CHAT_MESSAGE_PANEL::AddUserMessage( const wxString& aMessage )
{
    CHAT_MESSAGE_BUBBLE* bubble = new CHAT_MESSAGE_BUBBLE( this, true, aMessage, false );
    m_messages.push_back( bubble );

    // Calculate max bubble width before adding to sizer
    int panelWidth = GetClientSize().GetWidth();
    if( panelWidth <= 0 )
        panelWidth = 400;
    int maxBubbleWidth = panelWidth - ( m_horizontalPadding * 2 );
    if( maxBubbleWidth < 200 )
        maxBubbleWidth = 200;

    bubble->SetMaxSize( wxSize( maxBubbleWidth, -1 ) );

    // Add to sizer - don't use wxEXPAND, let bubble size itself based on content
    m_mainSizer->Add( bubble, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxTOP, m_horizontalPadding );
    m_mainSizer->AddSpacer( m_messageSpacing );

    // Update bubble layout now that it has a max size
    bubble->updateLayout();
    updateLayout();
    ForceScrollToBottom(); // User's own message - always scroll to show it
}


void CHAT_MESSAGE_PANEL::AddAIMessage( const wxString& aMessage, bool aIsHtml )
{
    CHAT_MESSAGE_BUBBLE* bubble = new CHAT_MESSAGE_BUBBLE( this, false, aMessage, aIsHtml );
    m_messages.push_back( bubble );

    // Calculate max bubble width before adding to sizer
    int panelWidth = GetClientSize().GetWidth();
    if( panelWidth <= 0 )
        panelWidth = 400;
    int maxBubbleWidth = panelWidth - ( m_horizontalPadding * 2 );
    if( maxBubbleWidth < 200 )
        maxBubbleWidth = 200;

    bubble->SetMaxSize( wxSize( maxBubbleWidth, -1 ) );

    // Add to sizer with wxEXPAND to use full width
    m_mainSizer->Add( bubble, 0, wxEXPAND | wxRIGHT | wxTOP, m_horizontalPadding );
    m_mainSizer->AddSpacer( m_messageSpacing );

    // Update bubble layout now that it has a max size
    bubble->updateLayout();
    updateLayout();
    ScrollToBottom();
}


void CHAT_MESSAGE_PANEL::AppendToLastAIMessage( const wxString& aText )
{
    // Find the last AI message
    for( int i = m_messages.size() - 1; i >= 0; i-- )
    {
        CHAT_MESSAGE_BUBBLE* bubble = m_messages[i];
        if( !bubble->IsUser() )
        {
            // Check if it's the "Thinking..." message and replace it
            wxString currentMsg = bubble->GetMessage();
            if( currentMsg == wxT( "Thinking..." ) )
            {
                // Replace the entire message with the new text
                bubble->SetMessage( aText, false );
            }
            else
            {
                // Append to existing message
                bubble->AppendText( aText );
            }
            updateLayout();
            ScrollToBottom();
            return;
        }
    }

    // No AI message found, create a new one
    AddAIMessage( aText, false );
}


void CHAT_MESSAGE_PANEL::AddExpandableSection( const wxString& aSummary, const std::vector<wxString>& aItems )
{
    EXPANDABLE_SECTION* section = new EXPANDABLE_SECTION( this, aSummary, aItems );

    // Add to sizer with wxEXPAND to use full width (removed wxLEFT)
    m_mainSizer->Add( section, 0, wxEXPAND | wxRIGHT, m_horizontalPadding );

    updateLayout();
    ScrollToBottom();
}


void CHAT_MESSAGE_PANEL::Clear()
{
    HideQueuedMessage();
    HideLoadingSkeleton();  // Must be called BEFORE Clear(true) to avoid dangling pointer
    HideStatusIndicator();  // Same for status indicator
    
    for( CHAT_MESSAGE_BUBBLE* bubble : m_messages )
    {
        bubble->Destroy();
    }
    m_messages.clear();
    m_mainSizer->Clear( true );
    updateLayout();
}


void CHAT_MESSAGE_PANEL::ShowQueuedMessage( const wxString& aMessage, std::function<void()> aOnCancel )
{
    // Hide existing queued message if any
    HideQueuedMessage();

    // Create new queued message panel
    m_queuedPanel = new QUEUED_MESSAGE_PANEL( this, aMessage, aOnCancel );

    // Add at the end of the sizer
    m_mainSizer->Add( m_queuedPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, m_horizontalPadding );

    updateLayout();
    ForceScrollToBottom();
}


void CHAT_MESSAGE_PANEL::HideQueuedMessage()
{
    if( m_queuedPanel )
    {
        m_mainSizer->Detach( m_queuedPanel );
        m_queuedPanel->Destroy();
        m_queuedPanel = nullptr;
        updateLayout();
    }
}


wxString CHAT_MESSAGE_PANEL::GetQueuedMessage() const
{
    if( m_queuedPanel )
        return m_queuedPanel->GetMessage();
    return wxEmptyString;
}


void CHAT_MESSAGE_PANEL::ShowTypingIndicator()
{
    if( m_typingIndicator )
        return; // Already showing

    m_typingIndicator = new TYPING_INDICATOR_PANEL( this );

    // Add to sizer aligned left (like AI messages)
    m_mainSizer->Add( m_typingIndicator, 0, wxALIGN_LEFT | wxRIGHT, m_horizontalPadding );

    m_typingIndicator->Start();

    updateLayout();

    // Scroll to show the indicator
    CallAfter(
            [this]()
            {
                ScrollToBottom();
            } );
}


void CHAT_MESSAGE_PANEL::HideTypingIndicator()
{
    if( m_typingIndicator )
    {
        m_typingIndicator->Stop();
        m_mainSizer->Detach( m_typingIndicator );
        m_typingIndicator->Destroy();
        m_typingIndicator = nullptr;
        updateLayout();
    }
}


void CHAT_MESSAGE_PANEL::ShowStatusIndicator( const wxString& aStatus )
{
    if( aStatus.IsEmpty() )
        return;

    // Hide any existing status indicator
    HideStatusIndicator();

    // Create new status indicator
    m_statusIndicator = new STATUS_INDICATOR_PANEL( this, aStatus );

    // Insert before the trailing spacer if possible to keep it close to the message
    size_t                 insertIdx = m_mainSizer->GetItemCount();
    const wxSizerItemList& children = m_mainSizer->GetChildren();
    if( !children.IsEmpty() )
    {
        wxSizerItem* lastItem = children.GetLast()->GetData();
        if( lastItem->IsSpacer() )
        {
            insertIdx = children.GetCount() - 1;
        }
    }

    m_mainSizer->Insert( insertIdx, m_statusIndicator, 0, wxALIGN_LEFT | wxRIGHT, m_horizontalPadding );

    updateLayout();
    ScrollToBottom();
}


void CHAT_MESSAGE_PANEL::HideStatusIndicator()
{
    if( m_statusIndicator )
    {
        m_mainSizer->Detach( m_statusIndicator );
        m_statusIndicator->Destroy();
        m_statusIndicator = nullptr;
        updateLayout();
    }
}


void CHAT_MESSAGE_PANEL::ShowLoadingSkeleton()
{
    // Hide any existing skeleton
    HideLoadingSkeleton();

    // Create new loading skeleton
    m_loadingSkeleton = new SHIMMER_SKELETON_PANEL( this );
    m_loadingSkeleton->Start();

    // Insert at the top of the sizer
    m_mainSizer->Insert( 0, m_loadingSkeleton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, m_horizontalPadding );

    updateLayout();
}


void CHAT_MESSAGE_PANEL::HideLoadingSkeleton()
{
    if( m_loadingSkeleton )
    {
        m_loadingSkeleton->Stop();
        m_mainSizer->Detach( m_loadingSkeleton );
        m_loadingSkeleton->Destroy();
        m_loadingSkeleton = nullptr;
        updateLayout();
    }
}


bool CHAT_MESSAGE_PANEL::IsNearBottom( int aThreshold ) const
{
    int xUnit, yUnit;
    GetScrollPixelsPerUnit( &xUnit, &yUnit );

    if( yUnit <= 0 )
        return true; // No scrolling, assume at bottom

    int x, y;
    GetViewStart( &x, &y );

    int currentScrollPos = y * yUnit;
    int virtualHeight = GetVirtualSize().GetHeight();
    int clientHeight = GetClientSize().GetHeight();
    int maxScrollPos = virtualHeight - clientHeight;

    if( maxScrollPos <= 0 )
        return true; // Content fits in view, no scrolling needed

    // Check if we're within threshold pixels of the bottom
    return ( maxScrollPos - currentScrollPos ) <= aThreshold;
}


void CHAT_MESSAGE_PANEL::ScrollToBottom()
{
    // Smart scrolling: only scroll if user is already near the bottom
    // This prevents interrupting users who scrolled up to read earlier messages
    if( IsNearBottom( 100 ) ) // 100px threshold
    {
        ForceScrollToBottom();
    }
}


void CHAT_MESSAGE_PANEL::ForceScrollToBottom()
{
    // Use CallAfter to ensure layout is complete before scrolling
    CallAfter(
            [this]()
            {
                int xUnit, yUnit;
                GetScrollPixelsPerUnit( &xUnit, &yUnit );

                if( yUnit <= 0 )
                    return;

                int maxY = GetVirtualSize().GetHeight() / yUnit;
                Scroll( 0, maxY );

                // Force a refresh to ensure scroll position is applied
                Refresh();
            } );
}


void CHAT_MESSAGE_PANEL::onSize( wxSizeEvent& aEvent )
{
    updateLayout();
    aEvent.Skip();
}


void CHAT_MESSAGE_PANEL::onMouseDown( wxMouseEvent& aEvent )
{
    // Start drag-to-scroll
    m_isDragging = true;
    m_dragStartPos = aEvent.GetPosition();
    
    int scrollX, scrollY;
    GetViewStart( &scrollX, &scrollY );
    m_dragStartScrollY = scrollY;
    
    // Capture mouse to receive events even when dragged outside the window
    if( !HasCapture() )
        CaptureMouse();
    
    // Don't skip the event - we're handling the drag ourselves
    // But allow the click to propagate for focus
    SetFocus();
}


void CHAT_MESSAGE_PANEL::onMouseUp( wxMouseEvent& aEvent )
{
    if( m_isDragging )
    {
        m_isDragging = false;
        
        if( HasCapture() )
            ReleaseMouse();
    }
    
    aEvent.Skip();
}


void CHAT_MESSAGE_PANEL::onMouseMove( wxMouseEvent& aEvent )
{
    if( m_isDragging && aEvent.LeftIsDown() )
    {
        // Calculate drag distance
        wxPoint currentPos = aEvent.GetPosition();
        int deltaY = m_dragStartPos.y - currentPos.y;
        
        // Get scroll rate to convert pixels to scroll units
        int pixelsPerUnitX, pixelsPerUnitY;
        GetScrollPixelsPerUnit( &pixelsPerUnitX, &pixelsPerUnitY );
        
        if( pixelsPerUnitY > 0 )
        {
            int newScrollY = m_dragStartScrollY + ( deltaY / pixelsPerUnitY );
            
            // Clamp to valid range
            int maxScrollY = GetScrollLines( wxVERTICAL );
            newScrollY = std::max( 0, std::min( newScrollY, maxScrollY ) );
            
            // Scroll to new position
            Scroll( -1, newScrollY );
        }
    }
    else if( m_isDragging && !aEvent.LeftIsDown() )
    {
        // Mouse button was released without us receiving the event (edge case)
        m_isDragging = false;
        
        if( HasCapture() )
            ReleaseMouse();
    }
    
    aEvent.Skip();
}


void CHAT_MESSAGE_PANEL::onMouseCaptureLost( wxMouseCaptureLostEvent& aEvent )
{
    // Mouse capture was lost (e.g., another window captured it)
    m_isDragging = false;
}


#ifdef __WXMSW__
void CHAT_MESSAGE_PANEL::onThemeChanged( wxSysColourChangedEvent& aEvent )
{
    // Windows: Update background color when system theme changes
    bool isDark = KIPLATFORM::UI::IsDarkTheme();
    if( isDark )
    {
        SetBackgroundColour( wxColour( 30, 30, 30 ) );
    }
    else
    {
        SetBackgroundColour( wxColour( 255, 255, 255 ) );
    }
    
    // Refresh all child bubbles to update their colors
    for( CHAT_MESSAGE_BUBBLE* bubble : m_messages )
    {
        bubble->Refresh();
    }
    
    // Refresh the panel itself
    Refresh();
    
    aEvent.Skip();
}
#endif


void CHAT_MESSAGE_PANEL::updateLayout()
{
    // Update bubble widths based on panel width
    int panelWidth = GetClientSize().GetWidth();
    if( panelWidth <= 0 )
    {
        // If panel width not set yet, use a default
        panelWidth = 400;
    }
    int maxBubbleWidth = panelWidth - ( m_horizontalPadding * 2 );

    // Ensure minimum bubble width
    if( maxBubbleWidth < 200 )
    {
        maxBubbleWidth = 200;
    }

    for( CHAT_MESSAGE_BUBBLE* bubble : m_messages )
    {
        bubble->SetMaxSize( wxSize( maxBubbleWidth, -1 ) );
        // Update the bubble's layout after setting max size so wrapping recalculates
        bubble->updateLayout();
    }

    Layout();

    // Enable scrolling by fitting the sizer inside the scrolled window
    // This automatically sets the virtual size based on the sizer's minimum size
    FitInside();

    Refresh();
}
