# SPDX-License-Identifier: LGPL-3.0-or-later
#-------------------------------------------------------------------------------
#
# Copyright Panasas, 2012
# Contributor: Jim Lieb <jlieb@panasas.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#
#-------------------------------------------------------------------------------
add_definitions(
  -D__USE_GNU
)

set( LIB_PREFIX 64)
if(NOT DEFINED VIVENAS_HOME)
	set(VIVENAS_HOME ../../../../ViveNAS)
endif()	
set(ROCKSDB_HOME ${VIVENAS_HOME}/rocksdb)
set(PUREFLASH_HOME  ${VIVENAS_HOME}/PureFlash)
set(ZOOKEEPER_SOURCE ${PUREFLASH_HOME}/thirdParty/zookeeper/zookeeper-client/zookeeper-client-c)

########### next target ###############
INCLUDE_DIRECTORIES( ${VIVENAS_HOME}/include ${ROCKSDB_HOME} ${ROCKSDB_HOME}/include ${PUREFLASH_HOME}/common/include ${PUREFLASH_HOME}/pfs/nlohmann_json/include )
LINK_DIRECTORIES(${VIVENAS_HOME}/pre_build_libs/${OS_NAME} ${VIVENAS_HOME}/build/bin ${PUREFLASH_HOME}/build_deb/bin ${ROCKSDB_HOME} ${ZOOKEEPER_SOURCE}/.libs) 
#LINK_DIRECTORIES(${VIVENAS_HOME}/build/bin ${PUREFLASH_HOME}/build_deb/bin ${ROCKSDB_HOME} ${ZOOKEEPER_SOURCE}/.libs)             )

SET(fsalvn_LIB_SRCS
   vn_export.c
   vn_handle.c
   vn_int.h
   vn_main.c
   vn_up.c
)

add_library(fsalvivenas SHARED ${fsalvn_LIB_SRCS})
add_sanitizers(fsalvivenas)

target_link_libraries(fsalvivenas
  ganesha_nfsd
  ${SYSTEM_LIBRARIES}
  ${LTTNG_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
  vivenas
)

set_target_properties(fsalvivenas PROPERTIES VERSION 1.0.0 SOVERSION 1)
install(TARGETS fsalvivenas COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

########### install files ###############
