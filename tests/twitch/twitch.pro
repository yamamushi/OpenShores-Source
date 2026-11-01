QT += core network gui testlib
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = twitch-connection-test
SOURCES += connection_test.cpp ../../src/lib/designer/settings/autwitchconnection.cpp
HEADERS += connection_test.h ../../src/lib/designer/settings/autwitchconnection.h
