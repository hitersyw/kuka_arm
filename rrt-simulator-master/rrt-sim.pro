#-------------------------------------------------
#
# Project created by QtCreator 2020-03-25T12:53:56
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = try
TEMPLATE = app

CONFIG += c++11

SOURCES += main.cpp mainwindow.cpp obstacles.cpp renderarea.cpp rrt.cpp rrtMult.cpp\
            bi_rrt.cpp rrtBiConnect.cpp rrtMultConnect.cpp rrtMultAdvance.cpp

HEADERS  += constants.h mainwindow.h obstacles.h\
            renderarea.h rrt.h ui_mainwindow.h rrtMult.h

FORMS    += mainwindow.ui
