# -*- mode: sh -*-
## Copyright (C) 2013 Intel Corportation
## Contact: http://www.qt-project.org/legal
##
## This file is part of the qtchooser module of the Qt Toolkit.
##
## $QT_BEGIN_LICENSE:BSD$
## You may use this file under the terms of the BSD license as follows:
##
## "Redistribution and use in source and binary forms, with or without
## modification, are permitted provided that the following conditions are
## met:
##   * Redistributions of source code must retain the above copyright
##     notice, this list of conditions and the following disclaimer.
##   * Redistributions in binary form must reproduce the above copyright
##     notice, this list of conditions and the following disclaimer in
##     the documentation and/or other materials provided with the
##     distribution.
##   * Neither the name of Digia Plc and its Subsidiary(-ies) nor the names
##     of its contributors may be used to endorse or promote products derived
##     from this software without specific prior written permission.
##
##
## THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
## "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
## LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
## A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
## OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
## SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
## LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
## DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
## THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
## (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
## OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
##
## $QT_END_LICENSE$
##

function qt_env_addto()
{
    eval $1="$2\${$1:+:\$$1}"
}

function qt_select()
{
    # Get or set the Qt version
    if [ $# -eq 0 ]; then
        # Get the Qt version
        if [ -z "$QT_SELECT" ]; then
            echo "Not using Qt."
        else
            echo "Using Qt version: $QT_SELECT"
        fi
    else
        # Set the working Qt version
        unset QT_SELECT
        local QTTOOLDIR
        test x$1 = xnone || eval $(qtchooser -qt=$1 -print-env) || return $?

        # Remove old
        qt_env_removefrom LD_LIBRARY_PATH $QTLIBDIR
        qt_env_removefrom PKG_CONFIG_PATH $QTLIBDIR/pkgconfig
        qt_env_removefrom CMAKE_PREFIX_PATH $QTDIR

        # Add new
        if [ x$1 != xnone ]; then
            qt_env_addto LD_LIBRARY_PATH $QTLIBDIR
            qt_env_addto PKG_CONFIG_PATH $QTLIBDIR/pkgconfig

            echo "Using Qt version: $1"
            export LD_LIBRARY_PATH PKG_CONFIG_PATH
            export QTLIBDIR QT_SELECT

            # try to get the QTDIR from qmake now
            if QTSRCDIR=$(qmake -query QT_INSTALL_PREFIX/src); then
                # Recent version of qmake that supports /get and /src
                QTDIR=$(qmake -query QT_INSTALL_PREFIX/get)
                export QTSRCDIR
            else
                # Older version
                QTDIR=$(qmake -query QT_INSTALL_PREFIX)

                # is this an uninstalled Qt build dir?
                if [ -f $QTDIR/Makefile ]; then
                    QTSRCDIR=$(dirname $(awk '/Project:/{print $NF}' $QTDIR/Makefile))
                    export QTSRCDIR
                else
                    unset QTSRCDIR
                fi
            fi
            export QTDIR

            qt_env_addto CMAKE_PREFIX_PATH $QTDIR
            export CMAKE_PREFIX_PATH
        else
            unset QTLIBDIR QTSRCDIR QTDIR QT_SELECT

            if qtchooser -print-env >/dev/null 2>&1; then
                echo "Using default Qt version."
            else
                echo "Not using Qt."
            fi
        fi
    fi
}

function qt() {
    qt_select "$@"
}

function qcd() {
    local dir="$1"
    shift 2>/dev/null
    cd $QTDIR/$dir "$@"
}

