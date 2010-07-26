#!/usr/bin/env python
# -*- coding: utf-8 -*-
# Copyright (C) 2010 Rosen Diankov (rosen.diankov@gmail.com)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
from openravepy import *
from numpy import *
from optparse import OptionParser
import os

def getsvnurl(dirname):
    outinfo=os.popen('svn info '+dirname,'r').read()
    url=None
    for line in outinfo.splitlines():
        if line.startswith('URL:'):
            url=line[4:].strip()
            break
    return url

if __name__ == "__main__":
    parser = OptionParser(description='Builds an rst file of the interfaces provided')
    parser.add_option('-o', action="store",type='string',dest='outdir',default='sphinx',
                      help='Output directory to write all interfaces reStructuredText files to, the root file is interfaces.rst (default=%default).')
    (options, args) = parser.parse_args()
    env=Environment()
    try:
        plugininfo=env.GetPluginInfo()
        text="""
----------
Interfaces
----------
"""
        interfaceinfo = {}
        for type in InterfaceType.values.values():
            interfaceinfo[type] = []
        for filename,info in plugininfo:
            dirname,libname = os.path.split(filename)
            pluginname = os.path.splitext(libname)[0]
            if pluginname.startswith('lib'):
                pluginname = pluginname[3:]
            info.dirname = dirname
            info.pluginname = pluginname
            for type,names in info.interfacenames:
                for name in names:
                    print name
                    interface = env.CreateInterface(type,name)
                    if interface is None:
                        print 'failed to create ',type,name
                    else:
                        try:
                            helptext = interface.SendCommand('help')
                            commands = interface.SendCommand('help commands').split()
                        except (openrave_exception,RuntimeError):
                            helptext = ''
                            commands = []
                            print e
                        ititle = name + ' - ' + pluginname
                        itext = '.. _%s-%s:\n\n'%(type,name.lower())
                        itext += ititle + '\n' + '-'*len(ititle) + '\n\n'
                        itext += ':Type: :ref:`interface-%s`\n\n'%(type)
                        itext += ':Plugin: :ref:`plugin-%s`\n\n'%(pluginname)
                        itext += interface.GetDescription() + '\n\n'
                        itext += helptext
                        interfaceinfo[type].append([name,pluginname,itext])
                        interface = None # destroy
        
        sortedtypes = interfaceinfo.keys()
        sortedtypes.sort(key=lambda x: str(x))
        for type in sortedtypes:
            descs = interfaceinfo[type]
            # sort by interface name (lowercase)
            descs.sort(key = lambda x: x[0].lower())
            typedir = os.path.join(options.outdir,'interfaces',str(type))
            mkdir_recursive(typedir)
            text += '.. _interface-%s:\n\n'%str(type) # link
            text += str(type) + '\n' + '-'*len(str(type)) + '\n.. toctree::\n  :maxdepth: 1\n  \n'
            for name,pluginname,itext in descs:
                text += '  interfaces/' + str(type) + '/' + name.lower() + '\n'
                interfacefile = os.path.join(typedir,name.lower()+'.rst')
                open(interfacefile,'w').write(itext)
            text += '\n\n'
        text += """
"""
        mkdir_recursive(options.outdir)
        open(os.path.join(options.outdir,'interfaces.rst'),'w').write(text)

        coreplugins = {}
        corepluginsdir = '../plugins'
        for filename in os.listdir(corepluginsdir):
            fullfilename = os.path.join(corepluginsdir,filename)
            if os.path.isdir(fullfilename):
                outstat=os.popen('svn stat -v --depth empty %s'%fullfilename,'r').read()
                revision = outstat.split()[1]
                coreplugins[filename] = (revision,getsvnurl(fullfilename))
        text = """
-------
Plugins
-------
"""
        # sort plugins by root name
        plugininfo.sort(key=lambda x: x[1].pluginname)
        for filename,info in plugininfo:
            print info.pluginname
            text += '.. _plugin-%s:\n\n'%info.pluginname # link
            text += info.pluginname + '\n' + '-'*len(info.pluginname) + '\n\n'
            text += 'Offers: '
            for type,names in info.interfacenames:
                for name in names:
                    text += ':ref:`%s:%s <%s-%s>` '%(str(type),name,str(type),name.lower())
            text += '\n\n'
            text += 'OpenRAVE Version: %s\n\n'%info.version
            if info.pluginname in coreplugins:
                revision,url = coreplugins[info.pluginname]
                text += 'Core Plugin: Last updated r%s\n\nURL: %s'%(revision,url)
            else:
                url=getsvnurl(os.path.join(info.dirname,'..'))
                if url is not None:
                    text += 'URL: '+url
            text += '\n\n'
        open(os.path.join(options.outdir,'plugins.rst'),'w').write(text)
    finally:
        env.Destroy()