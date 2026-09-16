#include "viewbbc/markers.h"

#include <string.h>

void viewbbc_markers_clear(ViewBBCMarkers *markers) {
    if (!markers) return;
    memset(markers, 0, sizeof(*markers));
}

int viewbbc_markers_set(ViewBBCMarkers *markers, unsigned number, size_t line, size_t column) {
    if (!markers || number < 1u || number > VIEWBBC_MARKER_COUNT) return 0;
    ViewBBCMarker *marker = &markers->marker[number - 1u];
    marker->line = line;
    marker->column = column;
    marker->set = 1;
    return 1;
}

int viewbbc_markers_unset(ViewBBCMarkers *markers, unsigned number) {
    if (!markers || number < 1u || number > VIEWBBC_MARKER_COUNT) return 0;
    markers->marker[number - 1u] = (ViewBBCMarker){0};
    return 1;
}

int viewbbc_markers_get(const ViewBBCMarkers *markers, unsigned number, size_t *line, size_t *column) {
    if (!markers || !line || !column || number < 1u || number > VIEWBBC_MARKER_COUNT) return 0;
    const ViewBBCMarker *marker = &markers->marker[number - 1u];
    if (!marker->set) return 0;
    *line = marker->line;
    *column = marker->column;
    return 1;
}
