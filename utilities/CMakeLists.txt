
#################
# BUILD UTILITIES
#################

add_executable(transmesh transmesh.c ../sources/libmeshb7.c)
target_compile_options (transmesh PRIVATE -DTRANSMESH)
target_link_libraries (transmesh Meshb.7 ${AIO_LIBRARIES})
install (TARGETS transmesh DESTINATION bin COMPONENT applications)

add_executable(mshinfo mshinfo.c)
target_link_libraries (mshinfo Meshb.7 ${AIO_LIBRARIES})
install (TARGETS mshinfo DESTINATION bin COMPONENT applications)

add_executable(speed_test speed_test.c)
target_link_libraries (speed_test Meshb.7 ${AIO_LIBRARIES})
install (TARGETS speed_test DESTINATION bin COMPONENT applications)

add_executable(ugrid2mesh ugrid2mesh.c)
target_link_libraries (ugrid2mesh Meshb.7 ${AIO_LIBRARIES})
install (TARGETS ugrid2mesh DESTINATION bin COMPONENT applications)

add_executable(stl2mesh stl2mesh.c)
target_link_libraries (stl2mesh Meshb.7 ${AIO_LIBRARIES})
install (TARGETS stl2mesh DESTINATION bin COMPONENT applications)

add_executable(bdf2mesh bdf2mesh.c)
target_link_libraries (bdf2mesh Meshb.7 ${AIO_LIBRARIES})
install (TARGETS bdf2mesh DESTINATION bin COMPONENT applications)

add_executable(mesh2stl mesh2stl.c)
target_link_libraries (mesh2stl Meshb.7 ${AIO_LIBRARIES})
install (TARGETS mesh2stl DESTINATION bin COMPONENT applications)

add_executable(mesh2bdf mesh2bdf.c)
target_link_libraries (mesh2bdf Meshb.7 ${AIO_LIBRARIES})
install (TARGETS mesh2bdf DESTINATION bin COMPONENT applications)

install (FILES libmeshb7_helpers.c DESTINATION helpers COMPONENT sources)
install (FILES libmeshb7_helpers.h DESTINATION include COMPONENT headers)
