//! What to do when the user closes the window. The page may have things to finish first (the options that wait for their quiet
//! period before they are written): once it says it is listening, the first request to close is answered by asking the page
//! and by arming a time limit, so a page that never answers cannot keep the window open.

use std::sync::atomic::{AtomicBool, Ordering};

/// What the window should do with a request to close it.
#[derive(Debug, PartialEq, Eq)]
pub enum CloseDecision {
    /// Let the window close now.
    Close,
    /// Keep the window, tell the page to finish up and close the window when it answers, and close it anyway after a time limit.
    Ask,
    /// The page was asked already and has not answered yet: keep the window and do nothing else.
    Wait,
}

#[derive(Default)]
pub struct CloseGate {
    listening: AtomicBool,
    asked: AtomicBool,
}

impl CloseGate {
    /// The page has a listener for the question and will answer it.
    pub fn page_listens(&self) {
        self.listening.store(true, Ordering::SeqCst);
    }

    pub fn on_close_requested(&self) -> CloseDecision {
        if !self.listening.load(Ordering::SeqCst) {
            return CloseDecision::Close;
        }
        if self.asked.swap(true, Ordering::SeqCst) {
            CloseDecision::Wait
        } else {
            CloseDecision::Ask
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_window_whose_page_does_not_listen_closes_at_once() {
        let gate = CloseGate::default();

        assert_eq!(gate.on_close_requested(), CloseDecision::Close);
        assert_eq!(gate.on_close_requested(), CloseDecision::Close, "every time, there is nothing to wait for");
    }

    #[test]
    fn a_page_that_listens_is_asked_the_first_time() {
        let gate = CloseGate::default();
        gate.page_listens();

        assert_eq!(gate.on_close_requested(), CloseDecision::Ask);
    }

    #[test]
    fn a_second_request_while_the_page_is_still_answering_only_waits() {
        let gate = CloseGate::default();
        gate.page_listens();
        gate.on_close_requested();

        assert_eq!(gate.on_close_requested(), CloseDecision::Wait);
        assert_eq!(gate.on_close_requested(), CloseDecision::Wait, "however often the user presses close");
    }

    #[test]
    fn listening_after_a_close_request_changes_the_next_answer_only() {
        let gate = CloseGate::default();
        assert_eq!(gate.on_close_requested(), CloseDecision::Close);

        gate.page_listens();

        assert_eq!(gate.on_close_requested(), CloseDecision::Ask);
    }
}
