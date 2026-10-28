cmake_minimum_required(VERSION 3.16)
project(lingmo-reader VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

option(BUILD_TESTING "Build the persistence tests" ON)

# QApplication: print dialog and the native file dialog
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Qml Quick DBus PrintSupport ShaderTools LinguistTools Multimedia Network)
# Poppler renders, searches and reads the text layer (small, already in the ISO)
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-qt6)
find_package(KF6Service REQUIRED)
find_package(KF6KIO REQUIRED)
# EPUB, DOCX and ODT are zip files
find_package(KF6Archive REQUIRED)

# Document backend, recent files and the annotation sidecars: shared by the app and the tests
add_library(reader-core STATIC
    src/pdfdocument.cpp
    src/searchmodel.cpp
    src/recentfiles.cpp
    src/annotationstore.cpp
    # EPUB, text, DOCX and ODT typeset into a PDF; what "Ouvir" reads
    src/bookconverter.cpp
    src/readingtext.cpp
)
target_include_directories(reader-core PUBLIC src)
target_link_libraries(reader-core PUBLIC Qt6::Core Qt6::Gui PkgConfig::POPPLER KF6::Archive)

add_executable(lingmo-reader
    src/main.cpp
    src/reader.cpp
    src/pageimageprovider.cpp
    # "Ouvir": read aloud through lingmo-voice-engine (lingmo-voicestudio)
    src/readaloud.cpp
    src/voiceclient.cpp
    qml.qrc
)

# Night / sepia reading modes
qt_add_shaders(lingmo-reader "reader-shaders"
    PREFIX "/"
    FILES shaders/readingmode.frag
)

target_link_libraries(lingmo-reader PRIVATE
    reader-core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::DBus Qt6::PrintSupport Qt6::Multimedia Qt6::Network
    KF6::Service KF6::KIOGui
)

set(TRANSLATIONS_DIR ${CMAKE_INSTALL_FULL_DATADIR}/lingmo-reader/translations)
target_compile_definitions(lingmo-reader PRIVATE
    TRANSLATIONS_DIR="${TRANSLATIONS_DIR}"
    LINGMO_READER_VERSION="${PROJECT_VERSION}"
)
# lrelease only: building never rewrites the .ts sources
qt_add_translation(QM_FILES translations/lingmo-reader_pt_BR.ts)
add_custom_target(translations ALL DEPENDS ${QM_FILES})
install(FILES ${QM_FILES} DESTINATION ${TRANSLATIONS_DIR})

install(TARGETS lingmo-reader RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES lingmo-reader.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(tst_recentfiles tests/tst_recentfiles.cpp)
    target_link_libraries(tst_recentfiles PRIVATE reader-core Qt6::Test)
    add_test(NAME tst_recentfiles COMMAND tst_recentfiles)

    add_executable(tst_annotations tests/tst_annotations.cpp)
    target_link_libraries(tst_annotations PRIVATE reader-core Qt6::Test)
    add_test(NAME tst_annotations COMMAND tst_annotations)

    # Poppler backend on a PDF written by QPdfWriter: text, selection, search, links
    add_executable(tst_document tests/tst_document.cpp)
    target_link_libraries(tst_document PRIVATE reader-core Qt6::Test)
    add_test(NAME tst_document COMMAND tst_document)

    # Sentences, headers/footers, footnotes and hyphenation on generated PDFs
    add_executable(tst_readingtext tests/tst_readingtext.cpp)
    target_link_libraries(tst_readingtext PRIVATE reader-core Qt6::Test)
    add_test(NAME tst_readingtext COMMAND tst_readingtext)

    # EPUB, text, DOCX and ODT typeset into PDFs with their table of contents
    add_executable(tst_bookconverter tests/tst_bookconverter.cpp)
    target_link_libraries(tst_bookconverter PRIVATE reader-core Qt6::Test)
    add_test(NAME tst_bookconverter COMMAND tst_bookconverter)

    set_tests_properties(tst_recentfiles tst_annotations tst_document tst_readingtext tst_bookconverter
                         PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C")
endif()
