#pragma once
// wm_ingest — m2 lean upload: read quickSnap manifest (/tmp/media/info.json)
// and build desc JSON via manifest::createDescInfoFile, written to the scan
// directory for UploadTask to pick up (wm-app-spec §2.1).

#include <string>

namespace app_workmode {

// Read /tmp/media/info.json (quickSnap manifest {files:[..], dir:"<ts>"}) →
// build full paths /tmp/media/<dir>/<file> → call manifest::createDescInfoFile
// → write desc to scanDir/<dir>.json.
// Returns: 1 = desc created, 0 = no manifest or empty, -1 = parse error,
// -2 = createDescInfoFile write failure.
int ingestQuickSnapManifest(const std::string& scanDir);

}  // namespace app_workmode
