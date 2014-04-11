TARGET = caldav-client
TEMPLATE = lib

QT       -= gui
QT       += network dbus

CONFIG += link_pkgconfig plugin debug console
PKGCONFIG += buteosyncfw5 libsignon-qt5 accounts-qt5 signon-oauth2plugin \
             libsailfishkeyprovider libkcalcoren-qt5 libmkcal-qt5 socialcache

VER_MAJ = 0
VER_MIN = 1
VER_PAT = 0

QMAKE_CXXFLAGS = -Wall \
    -g \
    -Wno-cast-align \
    -O2 -finline-functions

DEFINES += BUTEOCALDAVPLUGIN_LIBRARY

HEADERS += \
    caldavclient.h \
    buteo-caldav-plugin.h \
    report.h \
    put.h \
    delete.h \
    reader.h \
    settings.h \
    request.h \
    get.h \
    authhandler.h \
    incidencehandler.h \
    notebooksyncagent.h

SOURCES += \
    caldavclient.cpp \
    report.cpp \
    put.cpp \
    delete.cpp \
    reader.cpp \
    settings.cpp \
    request.cpp \
    get.cpp \
    authhandler.cpp \
    incidencehandler.cpp \
    notebooksyncagent.cpp

OTHER_FILES += \
    xmls/client/caldav.xml \
    xmls/sync/caldav-sync.xml

!contains (DEFINES, BUTEO_OUT_OF_PROCESS_SUPPORT) {
    target.path = /usr/lib/buteo-plugins-qt5
}

contains (DEFINES, BUTEO_OUT_OF_PROCESS_SUPPORT) {
    target.path = /usr/lib/buteo-plugins-qt5/oopp
    DEFINES += CLIENT_PLUGIN
    DEFINES += "CLASSNAME=CalDavClient"
    DEFINES += CLASSNAME_H=\\\"caldavclient.h\\\"
    INCLUDE_DIR = $$system(pkg-config --cflags buteosyncfw5|cut -f2 -d'I')

    HEADERS += $$INCLUDE_DIR/ButeoPluginIfAdaptor.h   \
               $$INCLUDE_DIR/PluginCbImpl.h           \
               $$INCLUDE_DIR/PluginServiceObj.h

    SOURCES += $$INCLUDE_DIR/ButeoPluginIfAdaptor.cpp \
               $$INCLUDE_DIR/PluginCbImpl.cpp         \
               $$INCLUDE_DIR/PluginServiceObj.cpp     \
               $$INCLUDE_DIR/plugin_main.cpp
}

sync.path = /etc/buteo/profiles/sync
sync.files = xmls/sync/*

client.path = /etc/buteo/profiles/client
client.files = xmls/client/*

INSTALLS += target sync client
