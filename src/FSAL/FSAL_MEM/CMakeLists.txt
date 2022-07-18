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
set(VIVENAS_HOME ../../../../ViveNAS)
set(ROCKSDB_HOME ${VIVENAS_HOME}/rocksdb)
set(PUREFLASH_HOME  ${VIVENAS_HOME}/PureFlash)
set(ZOOKEEPER_SOURCE ${PUREFLASH_HOME}/thirdParty/zookeeper/zookeeper-client/zookeeper-client-c)

########### next target ###############
INCLUDE_DIRECTORIES( ${VIVENAS_HOME}/include ${ROCKSDB_HOME} ${ROCKSDB_HOME}/include ${PUREFLASH_HOME}/common/include ${PUREFLASH_HOME}/pfs/nlohmann_json/include )
LINK_DIRECTORIES(/root/v2/ViveNAS/build/bin ) 
#LINK_DIRECTORIES(${VIVENAS_HOME}/build/bin ${PUREFLASH_HOME}/build_deb/bin ${ROCKSDB_HOME} ${ZOOKEEPER_SOURCE}/.libs)             )

SET(fsalmem_LIB_SRCS
   mem_export.c
   mem_handle.c
   mem_int.h
   mem_main.c
   mem_up.c
)

add_library(fsalmem SHARED ${fsalmem_LIB_SRCS})
add_sanitizers(fsalmem)

target_link_libraries(fsalmem
  ganesha_nfsd
  ${SYSTEM_LIBRARIES}
  ${LTTNG_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
  fsalvivenas
)

set_target_properties(fsalmem PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalmem COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

########### install files ###############
