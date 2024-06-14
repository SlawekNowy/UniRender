include(${CMAKE_SOURCE_DIR}/cmake/pr_common.cmake)

set(PROJ_NAME util_raytracing)
pr_add_library(${PROJ_NAME} SHARED)

pr_add_dependency(${PROJ_NAME} util_ocio TARGET PUBLIC)
pr_add_dependency(${PROJ_NAME} util_udm TARGET PUBLIC)

pr_add_external_dependency(${PROJ_NAME} openimagedenoise LIBRARY PUBLIC)
pr_add_external_dependency(${PROJ_NAME} opensubdiv LIBRARY PUBLIC)
pr_add_external_dependency(${PROJ_NAME} opensubdiv_cpu LIBRARY PUBLIC LINK_ONLY)

pr_add_headers(${PROJ_NAME} "include/")
pr_add_sources(${PROJ_NAME} "src/")

pr_add_compile_definitions(${PROJ_NAME} -DOIIO_STATIC_BUILD -DGLEW_STATIC -DRTUTIL_DLL)
pr_add_compile_definitions(${PROJ_NAME} -DCCL_NAMESPACE_BEGIN=namespace\ ccl\ {)
pr_add_compile_definitions(${PROJ_NAME} -DCCL_NAMESPACE_END=})

if(WIN32)
	target_link_libraries(${PROJ_NAME}
		PUBLIC
		opengl32
		glu32
	)
endif()

pr_finalize(${PROJ_NAME})
