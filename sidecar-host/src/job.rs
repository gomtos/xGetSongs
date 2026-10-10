//! Ties the sidecar's life to this process: when this process ends, however it ends (closed, crashed, killed), Windows ends
//! the processes in the job too. That reaches the grandchildren (yt-dlp, ffmpeg) that closing stdin does not.
//!
//! There is no unit test: the effect is on the process that calls it. Task 7 (M3) checks it with a real force-kill.

#[cfg(windows)]
pub fn kill_children_when_this_process_ends() -> Result<(), String> {
    let job = win32job::Job::create().map_err(|error| error.to_string())?;
    let mut info = job.query_extended_limit_info().map_err(|error| error.to_string())?;
    info.limit_kill_on_job_close();
    job.set_extended_limit_info(&mut info).map_err(|error| error.to_string())?;
    job.assign_current_process().map_err(|error| error.to_string())?;
    // Dropping the job would close its handle, and with it end this very process. The handle has to live as long as the
    // process does; Windows closes it when the process ends.
    std::mem::forget(job);
    Ok(())
}

#[cfg(not(windows))]
pub fn kill_children_when_this_process_ends() -> Result<(), String> {
    Ok(())
}
