# ******************************************************
# Michael Cohen <scudette@users.sourceforge.net>
#
# ******************************************************
#  Version: FLAG $Version: 0.82 Date: Sat Jun 24 23:38:33 EST 2006$
# ******************************************************
#
# * This program is free software; you can redistribute it and/or
# * modify it under the terms of the GNU General Public License
# * as published by the Free Software Foundation; either version 2
# * of the License, or (at your option) any later version.
# *
# * This program is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with this program; if not, write to the Free Software
# * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
# ******************************************************

""" This utility allows the user to rapidly query the offline whois
database which is loaded within the pyflag db.
"""
from optparse import OptionParser
import pyflag.conf
config=pyflag.conf.ConfObject()
import pyflag.pyflaglog as pyflaglog
import plugins.LogAnalysis.Whois as Whois
import pyflag.DB as DB
import textwrap

parser = OptionParser(usage = """%prog [options] [ip_address]

This will resolve the ip address against the internal offline database loaded into pyflag.
""")

parser.add_option("-f", "--file", default=None, 
                  help = "A file to read addresses from (one per line)")

(options, args) = parser.parse_args()
pyflaglog.start_log_thread()

dbh = DB.DBO()
def print_address(address):
    whois_id = Whois.lookup_whois(address)
    dbh.execute("SELECT INET_NTOA(start_ip) as start_ip, netname, numhosts, country, descr, remarks, adminc, techc, status from whois where id=%s limit 1",whois_id)
    row = dbh.fetch()
    row['ip'] = address
    row['descr'] = '\ndescr:          '.join([ x for x in row['descr'].splitlines()])
    print "------ %(ip)s ------\nnetname:        %(netname)s\nCountry:        %(country)s\ninetnum:        %(start_ip)s\nhosts:          %(numhosts)s\ndescr:          %(descr)s\n" % row

## Resolve all args:
for address in args:
    print_address(address)

if options.file:
    fd = open(options.file)
    for line in fd:
        line = line.strip()
        print_address(line)