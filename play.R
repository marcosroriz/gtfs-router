library(gtfsrouter)
packageVersion("gtfsrouter")

gtfs <- extract_gtfs("gtfs_mod.zip")
gtfs <- gtfs_timetable(gtfs, day = 2)
gtfs$transfers <- gtfs_transfer_table(
  gtfs,
  d_limit = 500,
  min_transfer_time = 120,
  # network = NULL,
  # network_times = TRUE
)

# gtfs$transfers$from_stop_id_tx <- lapply(gtfs$transfers$from_stop_id, function(i) { which(i == gtfs$stop_ids)[[1]] })
# gtfs$transfers$to_stop_id_tx <- lapply(gtfs$transfers$to_stop_id, function(i) { which(i == gtfs$stop_ids)[[1]] })

infrom <- "31502"
into <- "06363"

gtfs_route (
  gtfs,
  from = infrom,
  to = into,
  from_to_are_ids = TRUE,
  start_time = 12 * 3600
) # 12:02 in seconds

