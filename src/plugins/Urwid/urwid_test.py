""" This plugin is a proof of concept urwid application. It is
incorporated into PyFlag
"""
import pyflag.Reports as Reports
import cStringIO

try:
    import urwid
    import pyflag_display

    Screen = pyflag_display.Screen
except ImportError:
    disabled = True

class GUI:
    def __init__(self, fd):
        self.fd = fd
        fd.seek(0,2)
        self.size = fd.tell()
        self.file_offset = 0
        self.mark = 0
        self.end_mark = -1
        self.focus_column = 1
        self.screen_cache = None
        self.screen_offset = -1

    def run(self):
        self.ui = Screen()
        self.ui.set_mouse_tracking()
        self.ui.register_palette([
            ('header', 'black', 'dark cyan', 'standout'),
            ('body','black','light gray', 'standout'),
            ('editcp','black','light gray', 'standout'),
            ('default', 'default', 'default', 'bold'),
            ('editfc','white', 'dark blue', 'bold'),
            ('editbx','light gray', 'dark blue'),
            ('self', 'default', 'default'),
            ('help', 'yellow', 'default'),
            ])
        self.ui.start()
        return self.urwid_run()

    def cache_screen(self, offset, length):
        if self.screen_offset != offset:
            self.fd.seek(max(0,self.file_offset))
            self.screen_cache = cStringIO.StringIO(self.fd.read(length))
            self.screen_offset = offset

        self.screen_cache.seek(0)
            
    def refresh_screen(self):
        """ Redraws the whole screen with the current channel window
        set to channel
        """
        width, height = self.ui.get_cols_rows()

        offset_length = max(4,len("%X" % (self.file_offset))+1)
        ## We work out how much space is available for the hex edit area:
        ## This is the formula:
        ## width = offset_length + 3 * x + x (where x is the number of chars per line)
        x = (width - offset_length-1)/4
        offsets = []
        hexarea = []
        chars = []

        self.cache_screen(self.file_offset, width * height)
        offset = self.file_offset
        row_count = 1
        while 1:
            data = self.screen_cache.read(x)
            if len(data)==0: break

            ## Write the offset:
            offsets.append(("%%0%uX" % offset_length) % offset)

            ## Now write the hex area:
            hex_text = ''.join(["%02X " % ord(c) for c in data])
            hexarea.append(hex_text)

            ## Now write the chars:
            result = []
            for c in data:
                if c.isalnum() or c in "!@#$%^&*()_ +-=[]\{}|;':\",./<>?":
                    result.append(c)
                else:
                    result.append(".")
                    
            chars.append(''.join(result))

            row_count +=1
            offset += len(data)
            
            if row_count > height-1:
                self.row_size = x 
                break
            
        self.offsets = urwid.ListBox([urwid.Text("\n".join(offsets))])
        self.hex     = urwid.Edit('',"".join(hexarea), wrap='any', multiline=True)
        self.chars   = urwid.Edit('',''.join(chars), wrap='any', multiline=True)
        hex          = urwid.ListBox([urwid.AttrWrap(self.hex ,'editbx', 'editfc')])
        chars        = urwid.ListBox([urwid.AttrWrap(self.chars,'editbx','editfc')])
        self.columns = urwid.Columns([('fixed', offset_length+1, self.offsets),
                                      ('fixed', 3*x, hex), 
                                      ('fixed', x, chars),
                                      ],0,min_width = 11, focus_column=self.focus_column)

        top = urwid.AttrWrap(self.columns, 'body')
        
        self.status_bar = urwid.Text('')
        self.top = urwid.Frame(top, footer=urwid.AttrWrap(self.status_bar, 'header'))
        
    def urwid_run(self):
        self.refresh_screen()
        width, height = self.ui.get_cols_rows()

        while 1:
            self.hex.set_edit_pos((self.mark - self.file_offset)*3)
            self.chars.set_edit_pos(self.mark - self.file_offset)
            self.columns.set_focus_column(self.focus_column)
            self.status_bar.set_text(
                "Hex Edit: %u/%u (0x%X/0x%X)" % (self.file_offset + self.mark,
                                                 self.size,
                                                 self.file_offset + self.mark,
                                                 self.size))
            
            pagesize = self.row_size * (height - 5)
            self.draw_screen( (width, height) )

            ## We are a generator and we are ready for more input
            keys = self.ui.get_input((yield "Ready"))
            if "f8" in keys:
                return
            
            for k in keys:
                if urwid.is_mouse_event(k):
                    event, button, col, row = k
                    self.process_mouse_event(width, height, event, button, col, row)
#                    self.top.mouse_event( (width,height), event, 
#                                           button, col, row, focus=True )
                elif k=='page down':
                    if self.file_offset + pagesize < self.size:
                        self.file_offset += pagesize
                        self.mark += pagesize
                    self.refresh_screen()
                elif k=='page up':
                    self.file_offset = max(0, self.file_offset - pagesize)
                    self.mark = max(0, self.mark - pagesize)
                    self.refresh_screen()
                elif k=='right':
                    if self.mark +1 < self.size:
                        self.mark += 1
                elif k=='left':
                    self.mark = max(0,self.mark-1)
                elif k=='up':
                    self.mark = max(0,self.mark - self.row_size)
                    if self.mark < self.file_offset:
                        self.file_offset = max(0, self.file_offset -pagesize)
                        self.refresh_screen()
                elif k == 'window resize' or k=='ctrl l':
                    width, height = self.ui.get_cols_rows()
                    self.refresh_screen()
                elif k=='down':
                    if self.mark + self.row_size < self.size:
                        self.mark += self.row_size
                elif k=='tab':
                    if self.focus_column==1:
                        self.focus_column = 2
                    else: self.focus_column =1
                elif k=='<' or k=='home' or k=='meta <':
                    self.mark = 0
                    self.file_offset =0
                    self.refresh_screen()
                elif k=='>' or k=='end' or k=='meta >':
                    self.file_offset = self.size - (self.size % self.row_size)
                    self.mark = self.file_offset
                    self.refresh_screen()
#                else:
#                    print k
                    
                if self.mark > self.file_offset + pagesize:
                    self.file_offset += pagesize
                    self.refresh_screen()
                        
                    
            ## Other keys are just passed into the edit box
            ##    self.top.keypress( (width, height), k )

            
    def draw_screen(self, size):
        ## Refresh the screen:
        canvas = self.top.render( size, focus=True )
        self.ui.draw_screen( size, canvas )

    def process_mouse_event(self, width, height, event, button, col, row):
        if event=="mouse press" and button==1:
            widths = self.columns.column_widths((width,height))
            total_width = 0
            for i in range(0,len(widths)):
                if col >= total_width and col < widths[i]:
                    break

                total_width += widths[i]

            x = col - total_width                
            if i==0:                self.mark = self.file_offset + self.row_size * row
            elif i==1:
                self.mark = self.file_offset + self.row_size * (row) + x/3
                self.focus_column = 1
            elif i==2:
                self.mark = self.file_offset + self.row_size * (row+1) + x
                self.focus_column = 2

import sys

class UrwidTest(Reports.report):
    """ A Test of urwid applications """
    parameters = {}
    name = "Urwid Test"
    family = "Test"

    def display(self, query, result):
        ## Create the application

        screen = GUI(open("/etc/passwd"))
        generator = screen.run()
        generator.next()

        def urwid_cb(query, result):
            result.decoration = "raw"
            if query.has_key("_value"):
                ## Look for key input. Note we are not allowed to send
                ## a screen update in response to key input? This
                ## seems a bit inefficient.
                if query.has_key('input'):
                    ## Schedule it a little
                    generator.send(query.get('_value',''))

                screen.refresh_screen()
                result.content_type = "text/plain"
                result.result = screen.ui.buffer
            else:
                result.content_type = "text/html"
                result.result = "".join(pyflag_display._html_page)                

        result.iframe(callback = urwid_cb)