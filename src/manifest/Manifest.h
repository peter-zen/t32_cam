#ifndef MANIFEST_MANIFEST_H
#define MANIFEST_MANIFEST_H

#include <string>
#include <vector>

namespace manifest {

// Build the 5-section desc JSON (F_UploadedTag, file_inf[], device{}, data{},
// network{}, signal{}) for the given media files. Output written to `desc_info`.
// Returns EC_SUCCESS (0) on success, -1 on time-format failure.
// (Signature unchanged from main_app.cpp:268.)
int generateDescInfo(std::vector<std::string>& files, std::string& desc_info);

// Generate desc info and write it to `desc_filename`. Returns EC_SUCCESS on
// success, EC_OPEN_FILE_FAILED (-5) if the file cannot be opened, or the
// generateDescInfo return on failure.
// (Signature unchanged from main_app.cpp:453.)
int createDescInfoFile(std::vector<std::string>& media_files,
                       const std::string& desc_filename);

} // namespace manifest

#endif // MANIFEST_MANIFEST_H
