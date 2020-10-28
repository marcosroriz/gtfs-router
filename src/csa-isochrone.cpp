#include "csa.h"

//' rcpp_csa_isochrone
//'
//' Calculate isochrones using Connection Scan Algorithm for GTFS data. Works
//' largely as rcpp_csa. Returns a list of integer vectors, with [i] holding
//' sequences of stations on a given route, the end one being the terminal
//' isochrone point, and [i+1] holding correpsonding trip numbers.
//'
//' All elements of all data are 1-indexed
//'
//' @noRd
// [[Rcpp::export]]
Rcpp::List rcpp_csa_isochrone (Rcpp::DataFrame timetable,
        Rcpp::DataFrame transfers,
        const size_t nstations,
        const size_t ntrips,
        const std::vector <size_t> start_stations,
        const int start_time, const int end_time)
{

    const int duration = end_time - start_time;

    // make start and end stations into std::unordered_sets to allow
    // constant-time lookup. stations are submitted as 0-based, while all other
    // values in timetable and transfers table are 1-based R indices, so all are
    // converted below to 0-based.
    std::unordered_set <size_t> start_stations_set;
    for (auto s: start_stations)
        start_stations_set.emplace (s);

    const size_t n = static_cast <size_t> (timetable.nrow ());

    // convert transfers into a map from start to (end, transfer_time). Transfer
    // indices are also converted to 0-based:
    std::unordered_map <size_t, std::unordered_map <size_t, int> > transfer_map;
    std::vector <size_t> trans_from = transfers ["from_stop_id"],
        trans_to = transfers ["to_stop_id"];

    std::vector <int> trans_time = transfers ["min_transfer_time"];
    for (size_t i = 0; i < static_cast <size_t> (transfers.nrow ()); i++)
    {
        if (trans_from [i] != trans_to [i])
        {
            std::unordered_map <size_t, int> transfer_pair; // station, time
            if (transfer_map.find (trans_from [i]) == transfer_map.end ())
            {
                transfer_pair.clear ();
                transfer_pair.emplace (trans_to [i], trans_time [i]);
                transfer_map.emplace (trans_from [i], transfer_pair);
            } else
            {
                transfer_pair = transfer_map.at (trans_from [i]);
                transfer_pair.emplace (trans_to [i], trans_time [i]);
                transfer_map [trans_from [i]] = transfer_pair;
            }
        }
    }

    CSA_Iso csa_iso (nstations + 1);

    for (size_t i = 0; i < start_stations.size (); i++)
    {
        csa_iso.earliest_connection [start_stations [i]] = start_time;
        // The following lines add transfer stations to the list of initial
        // starting stations, but this should NOT be done, because the algorithm
        // needs a single root path to start, and this effectively starts with
        // multiple root paths.
        /*
        if (transfer_map.find (start_stations [i]) !=
                transfer_map.end ())
        {
            std::unordered_map <size_t, int> transfer_pair =
                transfer_map.at (start_stations [i]);
            for (auto t: transfer_pair)
            {
                // Don't penalise these first footpaths:
                //earliest_connection [t.first] = start_time + t.second;
                earliest_connection [t.first] = start_time;
            }
        }
        */
    }

    // stations and trips are size_t because they're used as direct array indices.
    const std::vector <size_t> departure_station = timetable ["departure_station"],
        arrival_station = timetable ["arrival_station"],
        trip_id = timetable ["trip_id"];
    const std::vector <int> departure_time = timetable ["departure_time"],
        arrival_time = timetable ["arrival_time"];

    std::vector <bool> is_connected (ntrips, false);

    // trip connections:
    std::unordered_set <size_t> end_stations;
    bool start_time_found = false;
    int actual_start_time = INFINITE_INT, actual_end_time = INFINITE_INT;

    for (size_t i = 0; i < n; i++)
    {
        if (departure_time [i] < start_time)
            continue; // # nocov - these lines already removed in R fn.
        if (departure_time [i] > actual_end_time)
            break;

        // add all departures from start_stations_set:
        if (start_stations_set.find (departure_station [i]) !=
                start_stations_set.end () &&
                arrival_time [i] <= csa_iso.earliest_connection [arrival_station [i] ])
        {
            is_connected [trip_id [i] ] = true;

            bool filled = csaiso::fill_one_csa_iso (departure_station [i],
                    arrival_station [i], trip_id [i], departure_time [i],
                    arrival_time [i], csa_iso);
            csa_iso.trip_start_time [departure_station [i] ] = departure_time [i];
            csa_iso.trip_start_time [arrival_station [i] ] = departure_time [i];
            csa_iso.elapsed_time [departure_station [i]] = 0L;
            csa_iso.elapsed_time [arrival_station [i]] =
                arrival_time [i] - departure_time [i];
            
            if (filled) {
                end_stations.emplace (arrival_station [i]);
                if (!start_time_found)
                {
                    actual_start_time = departure_time [i];
                    // Scan up until twice the isochrone duration from the
                    // actual start time:
                    actual_end_time = 2 * (end_time - start_time) + actual_start_time;
                    start_time_found = true;
                }
            }
        }

        // main connection scan:
        if ((csa_iso.earliest_connection [departure_station [i] ] <= departure_time [i])
                || is_connected [trip_id [i]])
        {
            int elapsed = arrival_time [i] -
                csa_iso.trip_start_time [departure_station [i] ];
            if (elapsed < csa_iso.elapsed_time [arrival_station [i] ])
            {
                bool filled = csaiso::fill_one_csa_iso (departure_station [i],
                        arrival_station [i], trip_id [i], departure_time [i],
                        arrival_time [i], csa_iso);

                bool in_isochrone = (csa_iso.elapsed_time [arrival_station [i]] < duration);

                if (in_isochrone && filled) {
                    end_stations.emplace (arrival_station [i]);
                    end_stations.erase (departure_station [i]);
                }
            }

            if (transfer_map.find (arrival_station [i]) != transfer_map.end ())
            {
                for (auto t: transfer_map.at (arrival_station [i]))
                {
                    size_t trans_dest = t.first;
                    int ttime = arrival_time [i] + t.second;
                    if (ttime < csa_iso.earliest_connection [trans_dest])
                    {
                        // Note: transfers do not have a current_trip value
                        csa_iso.earliest_connection [trans_dest] = ttime;
                        csa_iso.elapsed_time [trans_dest] =
                            csa_iso.elapsed_time [arrival_station [i]] + t.second;
                        csa_iso.prev_stn [trans_dest] = arrival_station [i];
                        csa_iso.prev_time [trans_dest] = arrival_time [i];
                        csa_iso.prev_arrival_time [trans_dest] = arrival_time [i];
                        csa_iso.trip_start_time [trans_dest] =
                            csa_iso.trip_start_time [departure_station [i]];
                    }
                }
            }
            is_connected [trip_id [i]] = true;
        }
    }
   
    Rcpp::List res (3 * end_stations.size () + 1);
    size_t count = 0;
    int time;
    for (auto es: end_stations)
    {
        std::vector <int> trip_out, end_station_out, end_times_out;
        size_t i = es;
        int prev_time; // holds original departure time at end
        if (csa_iso.current_trip [i] == INFINITE_INT)
            continue;

        trip_out.push_back (static_cast <int> (csa_iso.current_trip [i]));
        end_station_out.push_back (static_cast <int> (i));
        while (i < INFINITE_INT)
        {
            time = csa_iso.prev_arrival_time [i];
            if (time < INFINITE_INT && csa_iso.current_trip [i] < INFINITE_INT) {
                end_times_out.push_back (static_cast <int> (time));
                end_station_out.push_back (static_cast <int> (i));
                trip_out.push_back (static_cast <int> (csa_iso.current_trip [i]));

                if (csa_iso.prev_time [i] < INFINITE_INT)
                    prev_time = csa_iso.prev_time [i];
            }
            i = csa_iso.prev_stn [static_cast <size_t> (i)];
        }
        
        end_times_out.push_back (prev_time);

        std::reverse (end_station_out.begin (), end_station_out.end ());
        std::reverse (end_times_out.begin (), end_times_out.end ());
        std::reverse (trip_out.begin (), trip_out.end ());

        res (3 * count) = end_station_out;
        res (3 * count + 1) = trip_out;
        res (3 * count++ + 2) = end_times_out;
    }
    res (static_cast <size_t> (res.length ()) - 1) = actual_start_time;

    return res;
}

bool csaiso::fill_one_csa_iso (
        const size_t &departure_station,
        const size_t &arrival_station,
        const size_t &trip_id,
        const int &departure_time,
        const int &arrival_time,
        CSA_Iso &csa_iso) {

    bool fill_vals = (arrival_time < csa_iso.earliest_connection [arrival_station]);
    if (!fill_vals) {
        // service at that time already exists, so only replace if trip_id of
        // csa_in is same as trip that connected to the departure station.
        // This clause ensures connection remains on same service in cases of
        // parallel services; see #48 and equivalent code in csa.cpp
        const size_t prev_trip = csa_iso.current_trip [departure_station];
        fill_vals = (prev_trip < INFINITE_INT &&
                trip_id == csa_iso.current_trip [departure_station]);
    }

    if (fill_vals) {
        csa_iso.earliest_connection [arrival_station] = arrival_time;
        csa_iso.elapsed_time [arrival_station] = arrival_time -
            csa_iso.trip_start_time [departure_station];
        csa_iso.current_trip [arrival_station] = trip_id;
        csa_iso.prev_stn [arrival_station] = departure_station;
        csa_iso.prev_time [arrival_station] = departure_time;
        csa_iso.prev_arrival_time [arrival_station] = arrival_time;
        // fill in trip_id from departure_station only for the start of trips:
        if (csa_iso.current_trip [departure_station] == INFINITE_INT)
            csa_iso.current_trip [departure_station] = trip_id;
        // propagate trip start time from departure to arrival station:
        csa_iso.trip_start_time [arrival_station] =
            csa_iso.trip_start_time [departure_station];
    }

    return (fill_vals);
}
