""" This is a parser for the table search widget. The parser
implements a simple language for structured queries depending on the
type of the columns presented.
"""
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
import re,struct

## The following are common column types which the parser can
## handle. ColumnTypes can be defined as plugins by extending the
## ColumnTypes base class
class ColumnType:
    """ Base class for column type searches """

    ## These are the symbols which will be treated literally
    symbols = {
        "=":"literal",
        "!=":"literal",
        "<=": "literal",
        ">=": "literal",
        "<": "literal",
        ">": "literal",
        }
    def parse(self, column, operator, arg):
        ## Try to find the method which handles this operator:
        method = self.symbols.get(operator,operator)
        try:
            method = getattr(self, "operator_"+method)
        except:
            raise RuntimeError("%s is of type %s and has no operator %r. Does it make sense to use this operator on this data?" % (column, ("%s"% self.__class__).split('.')[-1], operator))

        return method(column, operator, arg)

    def operator_literal(self, column,operator, arg):
        return "`%s` %s %r" % (column, operator, arg)

class TimestampType(ColumnType):
    def operator_after(self, column, operator, arg):
        ## FIXME Should parse arg as a date - for now pass though to mysql
        return "`%s` > %r" % (column, arg)

    def operator_before(self,column, operator, arg):
        ## FIXME Should parse arg as a date
        return "`%s` < %r" % (column, arg)

class IPType(ColumnType):
    """ Handles creating appropriate IP address ranges from a CIDR specification.

    Code and ideas were borrowed from Christos TZOTZIOY Georgiouv ipv4.py:
    http://users.forthnet.gr/ath/chrisgeorgiou/python/
    """
    # reMatchString: a re that matches string CIDR's
    reMatchString = re.compile(
        r'(\d+)' # first byte must always be given
        r'(?:' # start optional parts
            r'\.(\d+)' # second byte
            r'(?:'#  optionally third byte
                r'\.(\d+)'
                r'(?:' # optionally fourth byte
                    r'\.(\d+)'
                r')?'
            r')?' # fourth byte is optional
        r')?' # third byte is optional too
        r'(?:/(\d+))?$') # and bits possibly

    # masks: a list of the masks indexed on the /network-number
    masks = [0] + [int(-(2**(31-x))) for x in range(32)]

    def operator_netmask(self, column, operator, address):
        # Parse arg as a netmask:
        match = self.reMatchString.match(address)
        try:
            if not match:
                raise Exception
            else:
                    numbers = [x and int(x) or 0 for x in match.groups()]
                    # by packing we throw errors if any byte > 255
                    packed_address = struct.pack('4B', *numbers[:4]) # first 4 are in network order
                    numeric_address = struct.unpack('!i', packed_address)[0]
                    bits = numbers[4] or numbers[3] and 32 or numbers[2] and 24 or numbers[1] and 16 or 8
                    mask = self.masks[bits]
                    broadcast = (numeric_address & mask)|(~mask)
        except:
            raise ValueError("%s does not look like a CIDR netmask (e.g. 10.10.10.0/24)" % address)
        
        return " ( `%s` >= %r and `%s` <= %r ) " % (column, numeric_address, column, broadcast)

def eval_expression(types, column, operator, arg):
    print "Evaluating %s.%s(%r)" % (column,operator,arg)
    try:
        column_type_cls = types[column]
    except:
        raise RuntimeError("Column %s not known" % column)

    ## Try to instantiate it:
    return column_type_cls.parse(column, operator, arg)

%%
parser SearchParser:
    ignore:    "[ \r\t\n]+"
    
    token END: "$"
    token STR: r'"([^\\"]+|\\.)*"'
    token STR2: r"'([^\\']+|\\.)*'"
    token WORD: r'[-+*/!@$%^&=.a-zA-Z0-9_]+'
    token LOGICAL_OPERATOR: "(and|or)"

    rule goal<<types>>: clause<<types>>  END {{ return clause }}

    ## A clause is a sequence of expressions seperated by logical
    ## operators. Since our operators are the same as SQL, we just
    ## copy them in.
    rule clause<<types>>: expr<<types>> {{ result = expr }}
                    (
                        LOGICAL_OPERATOR {{ logical_operator = LOGICAL_OPERATOR }}
                        expr<<types>> {{ result = "%s %s %s" % (result, logical_operator, expr) }}
                     )*  {{ return result }}

    ## A term may be encapsulated with " or ' or not. Note that
    ## strings use python to parse out the escape sequences so you can
    ## put \n,\r etc
    rule term: STR {{ return eval(STR) }}
                     | STR2 {{ return eval(STR2) }}
                     | WORD {{ return WORD }}

    ## The basic syntax is: column operator argument . This may also
    ## be encapsulated in ( ) in order to be put into the
    ## clause. Since out operators have the same precendence as SQL we
    ## just need to preserve the ( ).
    rule expr<<types>>: term {{ column = term }}
                     WORD {{ operator = WORD }}
                     term {{ return  eval_expression(types, column,operator,term)}}
                     #Preserve parenthases
                     | '\\(' clause<<types>> '\\)' {{ return "( %s )" % clause }}

%%

def parse_to_sql(text, types):
    P = SearchParser(SearchParserScanner(text))
    return runtime.wrap_error_reporter(P, 'goal', types)

if __name__=='__main__':
    types = {
        'Timestamp': TimestampType(),
        'IP Address': IPType(),
        }

    test = 'Timestamp after "2006-10-01 \\\"10:10:00\\\"" or (Timestamp before \'2006-11-01 "10:10:00"\' and  "IP Address" netmask "10.10.10.0/24") or "IP Address" = 192.168.1.1'
    print "Will test %s" % test
    print parse_to_sql(test,types)
