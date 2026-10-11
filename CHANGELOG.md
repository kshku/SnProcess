# Changelog

## [0.1.0] - 2026-10-11

### Added
- Anonymous and bidirectional byte pipes via `sn_pipe_create`,
  `sn_pipe_create_duplex`, `sn_pipe_read`, `sn_pipe_write`, `sn_pipe_wait`,
  and `sn_pipe_close`
- Process creation and supervision via `sn_process_spawn`,
  `sn_process_wait`, `sn_process_is_alive`, `sn_process_kill`,
  `sn_process_signal`, `sn_process_close`, `sn_process_get_id`,
  `sn_process_get_current_id`, `sn_process_get_parent_id`, and
  `sn_process_exit`
- stdio redirection through `SnProcessStdio` (inherit, pipe, file, or null)
- Shell helper `sn_process_spawn_shell` and streaming capture helper
  `sn_process_run`
