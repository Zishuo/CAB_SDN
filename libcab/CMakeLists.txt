include_directories(./)
aux_source_directory(. src_list)
add_library(cab ${src_list})

