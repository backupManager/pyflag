# ******************************************************
# Copyright 2004: Commonwealth of Australia.
#
# Developed by the Computer Network Vulnerability Team,
# Information Security Group.
# Department of Defence.
#
# Michael Cohen <scudette@users.sourceforge.net>
#
# ******************************************************
#  Version: FLAG $Version: 0.80.1 Date: Tue Jan 24 13:51:25 NZDT 2006$
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

""" Module defines base classes for reports and various classes used to manage them """

import pyflag.DB as DB
import pyflag.Registry as Registry
import pyflag.conf
config=pyflag.conf.ConfObject()
import pyflag.logging as logging

class ReportError(Exception):
    """ Base class for errors in reports """
    pass

class report:
    """ This is the main report base class for flag. Note that all reports should be subclassed from here, and should provide the following methods at least:

            - display - method responsible for displaying the results of the analysis
            - analyse - Method called to do the analysis in the background
            - progress - Used for indicating the progress for the user
            - form - used to display the form to the user

    The following attributes must be overridden:
            - name
            - family
            - description
            - parameters

    @cvar flag: Reference to the flag main object. This allows reports to control other reports through flag's dispatcher for example
    @cvar name: The name of this report to be placed in the main menu.
    @cvar description: A description of the purpose of the report.
    @cvar parameters: A dict stating the critical parameters of the report and their respective types
    @cvar ui: A UI object to be set by the dispatcher. All reports should use this UI reference to instantiate new UI objects.
    @cvar TypeChecker: A TypeCheck object instantiated by the dispatcher.
    @cvar DBO: A DBO reference allowing reports to use the DBO object.
    """
    name = "Base class"
    description = "Generic base report class"
    parameters = {}
    ui = None
    TypeChecker = None
    DBO = DB.DBO
    flag = None
    hidden = False
    order = 10

    def authenticate(self, query, result):
        """ This method is called first by FlagFramework to evaluate authentication and determine if the user is allowed to execute this report."""
        ## By default everyone is authorised
        try:
            ## If it exists config.AUTHORISE consists of a line looking like:
            ## authorise = mic:password,joe:letmein
            tokens = config.AUTHORISE.split(",")
            for token in tokens:
                try:
                    username, password = token.split(':')
                    if username.lstrip().rstrip()==query.user and password.lstrip().rstrip()==query.passwd:
                        logging.log(logging.DEBUG,"Accepted Authentication from %s" % query.user)
                        return True
                except ValueError:
                    logging.log(logging.WARNINGS,"Parse error in Authentication Token %r - ignoring this token " % token)
        except AttributeError,e:
            ## If config.AUTHORISE does not exist, we dont require authentication
            return True

        result.heading("Authentication denied")
        result.para("This report requires authentication by system administrator. Edit ~/.pyflagrc to add more usernames and passwords")
        return False

    def is_executing(self,canonical_query):
        """ Checks to see if a report is executing curently by looking at the lock data structure. If the report is currently executing, we return a tuple, containing the thread name and the task dict. """
        try:
            for k,v in self.executing.items():
                for x,y in v.items():
                    if canonical_query in y:
                        print "found thread %s" % k
                        return k
        except TypeError:
            pass
        
        return None

    def analyse(self,query):
        """ This will be executed to produce the precached material.

        This function should set up any knowledge base or tables which will be needed for the display part.
        @param query: A query_type object.

        """
        pass
    
    def form(self,query,result):
        """ This will be executed to produce the form. You do not need to put the <form> tags or the submit button here. You need to offer as many input elements here as are required by the parameters section above. """
        result.heading("I am calling the form method")
        return result

    def display(self,query,result):
        """ This routine will be called in the second phase to display the results """
        result.heading("I am calling the display method")
        return result

    def progress(self,query,result):
        """ This will be executed while the analysis is happening to give the user a progress bar """
        result.heading("Processing - please wait")
        return result

    def reset(self,query):
        """ Resets the report by doing all thats needed to return the report to the pre-analysed state. This may mean deletion of cache tables etc. Note that derived classes do not need to touch the meta table at all, this is done by the do_reset() method. Derived classes simply need to do the right thing here """
        pass

    def do_reset(self,query):
        """ This method actively resets the report named in params.

        First a query string is constructed using the named parameters in params. These parameters should be as though the target report was called with those parameters.
        Next the reset method of the report is called and then the meta tables are adjusted accordingly. Derived classes probably do not need to override this method, although they may need to call it so that they can reset other reports """
        #Get a handle to the target report
        report = Registry.REPORTS.dispatch(query['family'],query['report'])
        report = report(self.flag)
        
        #Get the canonicalised string relative to the target report:
        canonical_query = self.flag.canonicalise(query)

        #Call the report's reset method to ensure it resets the reports state
        report.reset(query)

        #Get a handle to the case db and clear the executed status from the database
        dbh = self.DBO(query['case'])
        dbh.execute("delete from meta where property='report_executed' and value=%r",canonical_query)

    #This shows those canonicalised query strings which are currently
    #executing. Note that this is a class variable to let different
    #instances of the same class know when they are analysing.
    executing = {}

    def __init__(self,flag,ui=None):
        ## This is here to avoid circular dependancy
        import pyflag.TypeCheck as TypeCheck
        import pyflag.HTMLUI as UI
        if not ui: ui = UI.HTMLUI

        self.TypeChecker = TypeCheck.TypeChecker()
        self.ui = ui
        self.flag=flag

    def check_prereq(self,query):
        """ This method checks to see if the analysis method of the report given by query was already run. If it has, this function returns immediately, else the analysis method is called. Note that this function will block untill all the analysis is finished. """
        if self.flag.is_cached(query):
            return
        else:
            #Are we currently executing the report?
#            result = self.flag.check_progress(report,query)
            self.flag.run_analysis(Registry.REPORTS.dispatch(query['family'],query['report']),query)

            return

    def check_parameters(self,query):
        """This subroutine checks that the paramters presented in paramters are ok to be run in this report. It does this by iterating over the type testing functions in class TypeCheck.TypeChecker to ensure that the paramters conform.

        If a required paramter was not provided we return False

        If all is well, we return True.

        Note that individual reports may choose to override the TypeCheck class with specific tests if required. This may be done by altering the reports self.TypeChecker.
        """
        import pyflag.TypeCheck as TypeCheck

        for key,value in self.parameters.items():
            #This is done so we check all parameters wheather they are here or not
            if not query.has_key(key):
                return False

            try:
                 #Check the string for conformance with required type value
                 if self.TypeChecker.check_type(value,key,query) == False:
                     return False
                 
            except TypeCheck.ReportInvalidParamter, e:
                raise TypeCheck.ReportInvalidParamter , "Parameter %s: %s" % (key,e)

        #If we got here without an exception, all is well
        return True