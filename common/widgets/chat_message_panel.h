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

#ifndef CHAT_MESSAGE_PANEL_H
#define CHAT_MESSAGE_PANEL_H

#include <wx/scrolwin.h>
#include <wx/string.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <vector>
#include <functional>

class wxHtmlWindow;
class wxStaticText;
class wxButton;

/**
 * An expandable/collapsible section for showing truncated lists.
 */
class EXPANDABLE_SECTION : public wxPanel
{
public:
    EXPANDABLE_SECTION( wxWindow* aParent, const wxString& aSummary, const std::vector<wxString>& aItems );

    void SetExpanded( bool aExpanded );
    bool IsExpanded() const { return m_expanded; }
    void Toggle();

private:
    void onToggleClick( wxCommandEvent& aEvent );
    void updateLayout();

    bool                  m_expanded;
    wxString              m_summary;
    std::vector<wxString> m_items;
    wxButton*             m_toggleButton;
    wxStaticText*         m_expandedText;
    wxBoxSizer*           m_sizer;
};

/**
 * A custom message bubble widget for displaying a single chat message.
 * AI messages that exceed a character threshold are truncated with a "See more" button.
 */
class CHAT_MESSAGE_BUBBLE : public wxPanel
{
public:
    CHAT_MESSAGE_BUBBLE( wxWindow* aParent, bool aIsUser, const wxString& aMessage, bool aIsHtml = false );

    /**
     * Update the message content.
     * @param aMessage The new message text.
     * @param aIsHtml True if the message contains HTML.
     */
    void SetMessage( const wxString& aMessage, bool aIsHtml = false );

    /**
     * Append text to the current message (for streaming).
     * @param aText The text to append.
     */
    void AppendText( const wxString& aText );

    /**
     * Check if this is a user message.
     */
    bool IsUser() const { return m_isUser; }

    /**
     * Get the current message text.
     */
    wxString GetMessage() const { return m_message; }

    /**
     * Check if the message is currently expanded.
     */
    bool IsExpanded() const { return m_expanded; }

    /**
     * Toggle between expanded and collapsed state (for long messages).
     */
    void ToggleExpand();

    /**
     * Set the expanded state.
     */
    void SetExpanded( bool aExpanded );

    friend class CHAT_MESSAGE_PANEL; // Allow CHAT_MESSAGE_PANEL to access private members

    // Character threshold for truncation (only applies to AI messages)
    static const int TRUNCATE_THRESHOLD = 500;

private:
    void     onPaint( wxPaintEvent& aEvent );
    void     onSize( wxSizeEvent& aEvent );
    void     onExpandClick( wxCommandEvent& aEvent );
    void     updateLayout();
    void     updateDisplayedText();
    wxString getTruncatedText() const;
    bool     needsTruncation() const;

    bool          m_isUser;
    wxString      m_message; // Full message text
    bool          m_isHtml;
    bool          m_expanded; // Is the message currently expanded?
    wxHtmlWindow* m_htmlWindow;
    wxStaticText* m_textLabel;
    wxButton*     m_expandButton; // "See more" / "See less" button
    wxBoxSizer*   m_sizer;
};

/**
 * A typing indicator panel showing animated dots (like iMessage).
 * Appears while AI is processing/thinking.
 */
class TYPING_INDICATOR_PANEL : public wxPanel
{
public:
    TYPING_INDICATOR_PANEL( wxWindow* aParent );
    ~TYPING_INDICATOR_PANEL();

    void Start(); ///< Start the animation
    void Stop();  ///< Stop the animation

private:
    void onPaint( wxPaintEvent& aEvent );
    void onTimer( wxTimerEvent& aEvent );

    wxTimer* m_timer;
    int      m_dotIndex; ///< Which dot is "active" (0-2)
    bool     m_running;
};

/**
 * A subtle status indicator panel (like Cursor's "Thought for 38s").
 * Displays small, grey text without a full chat bubble.
 */
class STATUS_INDICATOR_PANEL : public wxPanel
{
public:
    STATUS_INDICATOR_PANEL( wxWindow* aParent, const wxString& aStatus );

    void     SetStatus( const wxString& aStatus );
    wxString GetStatus() const { return m_status; }

private:
    void onPaint( wxPaintEvent& aEvent );

    wxString      m_status;
    wxStaticText* m_statusText;
};

/**
 * A panel showing a queued message with a cancel button.
 * Appears when user types while AI is responding.
 */
class QUEUED_MESSAGE_PANEL : public wxPanel
{
public:
    QUEUED_MESSAGE_PANEL( wxWindow* aParent, const wxString& aMessage, std::function<void()> aOnCancel );

    void     SetMessage( const wxString& aMessage );
    wxString GetMessage() const { return m_message; }

private:
    void onPaint( wxPaintEvent& aEvent );
    void onCancelClick( wxCommandEvent& aEvent );

    wxString              m_message;
    wxStaticText*         m_label;
    wxStaticText*         m_messageText;
    wxButton*             m_cancelButton;
    std::function<void()> m_onCancel;
};

/**
 * A shimmer loading skeleton panel.
 * Shows animated placeholder boxes while content loads.
 * Modern loading UX pattern with gradient shimmer effect.
 */
class SHIMMER_SKELETON_PANEL : public wxPanel
{
public:
    SHIMMER_SKELETON_PANEL( wxWindow* aParent );
    ~SHIMMER_SKELETON_PANEL();

    void Start(); ///< Start the shimmer animation
    void Stop();  ///< Stop the shimmer animation

private:
    void onPaint( wxPaintEvent& aEvent );
    void onTimer( wxTimerEvent& aEvent );

    wxTimer* m_timer;
    float    m_shimmerOffset;  ///< Current shimmer position (0.0 to 1.0+)
    bool     m_running;
};

/**
 * A scrollable panel for displaying chat messages with modern bubble styling.
 */
class CHAT_MESSAGE_PANEL : public wxScrolledWindow
{
public:
    CHAT_MESSAGE_PANEL( wxWindow* aParent, wxWindowID aId = wxID_ANY, const wxPoint& aPos = wxDefaultPosition,
                        const wxSize& aSize = wxDefaultSize, long aStyle = wxVSCROLL );

    /**
     * Add a user message bubble.
     * @param aMessage The message text.
     */
    void AddUserMessage( const wxString& aMessage );

    /**
     * Add an AI message bubble.
     * @param aMessage The message text (can contain markdown/HTML).
     * @param aIsHtml True if the message is already HTML formatted.
     */
    void AddAIMessage( const wxString& aMessage, bool aIsHtml = false );

    /**
     * Append text to the last AI message (for streaming).
     * Creates a new AI message if none exists.
     * @param aText The text to append.
     */
    void AppendToLastAIMessage( const wxString& aText );

    /**
     * Add an expandable section (for "View X more" functionality).
     * @param aSummary The collapsed summary text (e.g., "... and 15 more")
     * @param aItems The full list of items to show when expanded
     */
    void AddExpandableSection( const wxString& aSummary, const std::vector<wxString>& aItems );

    /**
     * Show a queued message indicator with cancel button.
     * @param aMessage The queued message text.
     * @param aOnCancel Callback when cancel button is clicked.
     */
    void ShowQueuedMessage( const wxString& aMessage, std::function<void()> aOnCancel );

    /**
     * Hide and clear the queued message indicator.
     */
    void HideQueuedMessage();

    /**
     * Show the typing indicator (animated dots like iMessage).
     * Call this when AI starts processing.
     */
    void ShowTypingIndicator();

    /**
     * Hide the typing indicator.
     * Call this when AI response starts streaming or completes.
     */
    void HideTypingIndicator();

    /**
     * Check if typing indicator is currently showing.
     */
    bool IsTypingIndicatorVisible() const { return m_typingIndicator != nullptr; }

    /**
     * Show a subtle status indicator (like Cursor's "Thought for 38s").
     * Displays small, grey text without a full chat bubble.
     * @param aStatus The status text to display.
     */
    void ShowStatusIndicator( const wxString& aStatus );

    /**
     * Hide and clear the status indicator.
     */
    void HideStatusIndicator();

    /**
     * Check if status indicator is currently showing.
     */
    bool IsStatusIndicatorVisible() const { return m_statusIndicator != nullptr; }

    /**
     * Show a shimmer loading skeleton.
     * Displays animated placeholder boxes while messages load.
     */
    void ShowLoadingSkeleton();

    /**
     * Hide the loading skeleton.
     */
    void HideLoadingSkeleton();

    /**
     * Check if loading skeleton is currently showing.
     */
    bool IsLoadingSkeletonVisible() const { return m_loadingSkeleton != nullptr; }

    /**
     * Get the current queued message text (empty if none).
     */
    wxString GetQueuedMessage() const;

    /**
     * Check if there's a queued message showing.
     */
    bool HasQueuedMessage() const { return m_queuedPanel != nullptr; }

    /**
     * Clear all messages.
     */
    void Clear();

    /**
     * Scroll to the bottom of the chat (only if user is already near bottom).
     * This implements "smart scrolling" - won't interrupt if user scrolled up to read.
     */
    void ScrollToBottom();

    /**
     * Force scroll to bottom regardless of current position.
     */
    void ForceScrollToBottom();

    /**
     * Check if the view is currently near the bottom.
     * @param aThreshold How many pixels from bottom counts as "near" (default 50)
     */
    bool IsNearBottom( int aThreshold = 50 ) const;

private:
    void onSize( wxSizeEvent& aEvent );
    void updateLayout();
    
    // Drag-to-scroll handlers
    void onMouseDown( wxMouseEvent& aEvent );
    void onMouseUp( wxMouseEvent& aEvent );
    void onMouseMove( wxMouseEvent& aEvent );
    void onMouseCaptureLost( wxMouseCaptureLostEvent& aEvent );
    
#ifdef __WXMSW__
    void onThemeChanged( wxSysColourChangedEvent& aEvent );
#endif

    std::vector<CHAT_MESSAGE_BUBBLE*> m_messages;
    QUEUED_MESSAGE_PANEL*             m_queuedPanel;
    TYPING_INDICATOR_PANEL*           m_typingIndicator;
    STATUS_INDICATOR_PANEL*           m_statusIndicator;
    SHIMMER_SKELETON_PANEL*           m_loadingSkeleton;
    wxBoxSizer*                       m_mainSizer;
    int                               m_messageSpacing;
    int                               m_horizontalPadding;
    int                               m_lastPanelWidth; // Track width to avoid unnecessary layout updates
    
    // Drag-to-scroll state
    bool                              m_isDragging;
    wxPoint                           m_dragStartPos;
    int                               m_dragStartScrollY;
};

#endif // CHAT_MESSAGE_PANEL_H
