# 在 task_manager 的 CMakeLists.txt 中，创建 task_manager_node 可执行目标之后添加。
# third_party/audioplay 下面放本包完整源码；EKHO_SOURCE_DIR 指向已打补丁并编译的 Ekho 8.9。
set(AUDIOPLAY_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/audioplay)
target_link_libraries(task_manager AudioPlay::audioplay)
target_include_directories(task_manager PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/audioplay/integration/ros")
add_custom_command(TARGET task_manager_node POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:task_manager_node>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/audioplay/config/audioplay.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/audioplay/config/audio_manifest.json"
        "$<TARGET_FILE_DIR:task_manager_node>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/audioplay/audio"
        "$<TARGET_FILE_DIR:task_manager_node>/audio")
# 若使用 catkin install，配置要放在最终节点可执行文件旁边。
install(FILES third_party/audioplay/config/audioplay.json third_party/audioplay/config/audio_manifest.json DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}/config)
install(DIRECTORY third_party/audioplay/audio/ DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}/audio)
