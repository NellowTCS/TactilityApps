# Satellite Tracker

Shows satellites on a world map with their ground track, visibility footprint and the day and night areas, and predicts when they pass over your location.
Orbit data comes from [CelesTrak](https://celestrak.org) and the positions are calculated on the device with SGP4.

## Requirements

- **Clock:** the positions depend on the time, so the clock must be set. Connect to Wi-Fi once so Tactility can sync the time. Until then the map shows no satellites and the info line says that the clock is not set.
- **Wi-Fi:** needed to download orbit data, groups and search results. Without it the app keeps working with the data it downloaded before. Satellites without any orbit data yet (for example on the first start) are listed but not shown on the map until their data arrives.
- **Location:** the app follows Tactility's location services (GPS or network based) when they are turned on in Tactility's settings and have a fix. Otherwise it uses the location from the app's settings, which is New York City until you enter your own. If you clear that location and there is no device location, there are no pass predictions, no elevation values and no automatic selection of the next pass.

## Map screen

The main screen shows the map, a legend and an info area.

### Map

- World map (Natural Earth land outlines) with a 30 degree grid.
- Night side shaded darker, with a lighter band for twilight (sun up to 6 degrees below the horizon).
- Your location as an X: the device location, or the one from the app's settings (New York City until you set your own).
- Every tracked satellite as a marker with its own shape: six filled shapes, then the same six as outlines. On color screens each also has its own color.
- Markers that would cover each other (for example the POISK module docked to the ISS) are spread around their common spot.
- Satellites without a position (no orbit data yet, or orbit data that can no longer be used) have no marker and cannot be tapped on the map.
- The selected satellite gets a circle around its marker, its ground track and its footprint (the area where it is above the horizon, dotted). The track behind it is solid and covers half an orbit, the track ahead is dashed and covers one full orbit. For orbits longer than 6 hours (GNSS, geostationary) the track is limited to 3 hours behind and 6 hours ahead.
- With "Tracks for all satellites" in the settings, every satellite gets its track and footprint in its own color.

### Legend

One entry per satellite: marker, short name and a value:

- `up 34°`: the satellite is above your horizon now, at that elevation.
- `20:31`: the time when the next pass starts (passes lower than 5 degrees are skipped).
- `--`: no pass higher than 5 degrees in the next 36 hours, no orbit data yet, or orbit data that cannot be used any more.
- `418 km`: the altitude, shown instead of the above when no location is set.

The selected satellite has a solid frame, a satellite selected automatically has a dashed frame.

### Info area

- Name, catalog number, position, altitude, speed and whether the satellite is sunlit or in Earth's shadow. For a satellite without a position it says that its orbit data is still missing or can no longer be used.
- The pass: above the horizon now (elevation, direction, set time), the next pass (start and end time, highest elevation, rise and set direction, "tomorrow" when it is on the next day, the weekday when it is later), always above the horizon (geostationary satellites), no pass higher than 5 degrees in 36 hours, or "Set location in settings".
- The status line: when the map was updated and how old the orbit data is, or a message while downloading or after a failed download. When the clock was moved back after a download, the age is unknown and not shown.

### Layout

The app picks the layout that gives the biggest map:

- Legend in a grid below the map (narrow screens and most wide screens).
- Legend, details and status in a column next to the map (short, wide screens). The details and status keep their place at the bottom of the column, and the legend scrolls when there are more satellites than fit.

On screens at least 560 pixels wide the details are longer (for example the coordinates and speed).

### Selecting a satellite

- Tap a marker or a legend entry to select that satellite. Tap it again to unselect it.
- With a keyboard or trackball: when the map has the focus, left and right step through the satellites, including "nothing selected".
- With an encoder or the arrow keys: move the focus to a legend entry and press it to select or unselect that satellite.

### Automatic selection of the next pass

When no satellite is selected and "Select the next pass automatically" is on in the settings (default), the app shows the satellite whose pass is in progress or starts first.

- Only passes higher than 5 degrees within the next 36 hours count.
- When the pass ends, the next one is chosen at the next map update (on e-paper that can take up to the update interval).
- **Without a location** (no device location and the one in the settings cleared) there are no passes, so nothing is selected automatically.
- When no satellite has a pass in the next 36 hours, nothing is selected and the info area says so.
- Geostationary satellites that are always above the horizon are not chosen.
- Selecting a satellite yourself overrides the automatic choice until you unselect it.

### Bigger map

Tap an empty part of the map, or press the button (Enter) while the map has the focus, to hide the toolbar and make the map bigger. Do the same again to bring the toolbar back. The choice is remembered.

### Map updates

The map, legend and info area are updated every 5 seconds on color screens and every 60 seconds on e-paper. The interval can be changed in the settings. A tap or a new selection updates the map at once.

## Next passes screen

The calendar button lists the passes of all tracked satellites over your location in the next 24 hours, sorted by time.

- Each row shows the start and end time, the highest elevation, the rise and set direction and the satellite. Passes on the next day show the weekday.
- Passes lower than 5 degrees are skipped. At most 10 passes per satellite and 40 rows are listed.
- `* visible` marks passes where the satellite is in sunlight while your sky is dark (sun more than 6 degrees below the horizon). Whether you can see it also depends on its brightness and the weather, which the app does not know.
- Satellites that are always above the horizon are listed separately.
- The list is calculated when the screen opens and does not update by itself.
- It needs a location and a set clock, otherwise the screen says what is missing.

## Satellites screen

The list button opens the tracked satellites.

- Each row shows the marker, the short name and the full name from the orbit data, or the catalog number and "waiting for orbit data" while that is missing.
- **Rename:** sets the short name used in the legend (up to 15 characters). "Automatic" restores the name from the orbit data without the part in brackets (for example "ISS" for "ISS (ZARYA)"). Saving an empty name does the same. Both need the orbit data, so until it arrives "Automatic" is not offered and an empty name keeps the current one.
- **Remove:** stops tracking the satellite. Removing all satellites leaves an empty list, the default satellites are only added on the first start.
- **Add from group:** CelesTrak groups such as space stations, brightest, weather, NOAA, GOES, amateur radio, GPS, Galileo, GLONASS, BeiDou, science and CubeSats. Tap a satellite to add or remove it.
  - The list is sorted by name. Large groups are cut to the first 400 satellites read (150 on devices without external RAM) and 250 shown, use the search for others.
  - A group is downloaded again only when the saved copy is older than 24 hours. When that download fails, the saved copy is shown.
  - The groups are defined by CelesTrak. "Space stations", for example, also contains docked spacecraft and CubeSats launched from the ISS.
- **Search:** type a name (or part of it, for example "NOAA") or a NORAD catalog number, up to 31 characters. Up to 100 results (60 on devices without external RAM), sorted by name.

How many satellites can be tracked depends on the screen: twice the number of legend columns below the map, or the number of legend rows next to the map, up to 12 (6 on a 320 pixel wide screen). A list saved with more satellites (for example on a bigger screen) is kept, but no more can be added until some are removed.

On devices without a keyboard the on-screen keyboard opens for the text fields.

## Settings

- **Use the device location (GPS or network) when available:** on by default. While the app runs it keeps Tactility's location services active, which starts a GPS if the device has one. Movements below about 1 km are ignored, so the passes are not recalculated with every GPS update. The line below the switch says which location is in use: the device location with its accuracy and source, "waiting" while there is no fix yet, or that location services are off in Tactility's settings. Approximate location (about 3 km) is precise enough for passes.
- **Location:** used when the device location is off or not available. Latitude and longitude in decimal degrees, north and east positive, south and west negative. A comma works as decimal separator. Altitude in meters is optional. New York City (40.7128, -74.0060, 10 m) is used until you save your own. Clear latitude and longitude for no location.
- **Map update interval** in seconds, 1 to 3600. Empty means the default (5 seconds on color screens, 60 on e-paper).
- **Tracks for all satellites:** draws the track and footprint of every satellite, not only the selected one.
- **Select the next pass automatically when no satellite is selected:** see above.
- **Update orbit data:** downloads the orbit data of the tracked satellites again. It only works when the data is older than 2 hours, because CelesTrak asks clients not to download more often. Satellites that have no orbit data yet are downloaded in any case.

## Orbit data downloads

- **First start:** the app tracks the ISS, the Chinese space station (CSS), the Hubble Space Telescope and the XMM-Newton X-ray observatory. The list is saved at once, the orbit data is downloaded as soon as Wi-Fi and the clock are available.
- **Missing orbit data** (the first start, or a list without cached data) is downloaded as soon as possible. A failed download is tried again after 30 seconds, up to 3 times in total, then every 15 minutes while the app is open. When CelesTrak has no data for a satellite (re-entered, or a wrong number), it is asked again after a day.
- **Updates:** when the app starts and the data is older than 2 hours, it is downloaded again. While the app stays open, it is refreshed once a day. After a failed update the app keeps the old data and tries again after 15 minutes.
- A satellite removed while its download is running is not added back when the download finishes.
- Downloads need a set clock, because the server certificate cannot be checked without it.
- Orbit data gets less accurate with age. After a few weeks the predictions can be off by minutes, and for satellites that have re-entered the calculation fails and the legend shows `--`.

## Stored data

The app keeps its files in its user data folder (`user/app/tactility.satellitetracker`). Reinstalling or updating the app keeps them.

- `satellites.txt`: the tracked satellites, one per line as catalog number, marker slot and short name (`25544|0|ISS`). Written only when you add, remove or rename a satellite.
- `orbits.tle`: the latest orbit data of the tracked satellites in the usual three line format. Written once the downloads of a refresh are done and when the app closes, not after every single download.
- `settings.properties`: the settings and the time of the last orbit data download.
- `group-<name>.tle` and `search.tle`: the last group lists and search results. A download goes to a `.part` file first, so a failed download keeps the previous copy.
- Without `satellites.txt` the app starts with the default satellites. An empty `satellites.txt` means no satellites. Damaged lines are skipped, a satellite without usable cached orbit data is downloaded again.

## Limitations

- The passes assume a flat, free horizon. Mountains, buildings and atmospheric refraction are not taken into account.
- Times are shown in the device's time zone.

## Data and code from other sources

- Orbit data: [CelesTrak](https://celestrak.org). Please keep the download limits: the app downloads a group at most once a day and the tracked satellites at most every 2 hours.
- Map: [Natural Earth](https://www.naturalearthdata.com) 1:110m land polygons, public domain. `tools/gen_world_map.py` creates `main/Source/WorldMap.c` from `ne_110m_land.geojson`.
- SGP4: C port by aholinch (https://github.com/aholinch/sgp4) of the code from "Revisiting Spacetrack Report #3" by Vallado et al., released under the Unlicense. See `main/Source/sgp4`.
- `assets/celestrak.pem` contains the root certificates that sign celestrak.org (USERTrust RSA Certification Authority and Sectigo Public Server Authentication Root R46).
