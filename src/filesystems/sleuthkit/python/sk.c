/* Sleuthkit python module for use by pyflag
 * Contains two new types (skfs and skfile).
 * skfs implement an interface somewhat similar to python's 'os' module.
 * skfile implements a file-like object for accessing files through sk.
 */

#include <Python.h>

#include "sk.h"
#include "list.h"
#include "talloc.h"
#include "fs_tools.h"
#include "libfstools.h"
#include "ntfs.h"
 
/*
 * Suggested functions:
 * skfs:
 * lookup (return inode for a path)
 * walk (same as os.walk)
 * stat (return stat info for an path)
 * isdir (is this a dir)
 * islink (is this a link)
 * isfile (is this a file)
 * skfile:
 * read (read from path)
 */

static u_int8_t
inode_walk_callback(FS_INFO *fs, FS_INODE *fs_inode, int flags, void *ptr) {
    PyObject *inode, *list;
    list = (PyObject *)ptr;
    
    // add each ntfs data attribute
    if (((fs->ftype & FSMASK) == NTFS_TYPE) && (fs_inode)) {
        FS_DATA *fs_data;

        for(fs_data = fs_inode->attr; fs_data; fs_data = fs_data->next) {
            if(!(fs_data->flags & FS_DATA_INUSE))
                continue;

            if(fs_data->type == NTFS_ATYPE_DATA) {
                inode = (PyObject *)PyObject_New(skfs_inode, &skfs_inodeType);
                ((skfs_inode *)inode)->inode = fs_inode->addr;
                ((skfs_inode *)inode)->type = fs_data->type;
                ((skfs_inode *)inode)->id = fs_data->id;
                ((skfs_inode *)inode)->alloc = (flags & FS_FLAG_META_ALLOC) ? 1 : 0;

                PyList_Append(list, inode);
            }
        }
    } else {
        // regular filesystems dont have type-id, make them 0
        inode = (PyObject *)PyObject_New(skfs_inode, &skfs_inodeType);
        ((skfs_inode *)inode)->inode = fs_inode->addr;
        ((skfs_inode *)inode)->type = 0;
        ((skfs_inode *)inode)->id = 0;
        ((skfs_inode *)inode)->alloc = (flags & FS_FLAG_META_ALLOC) ? 1 : 0;

        PyList_Append(list, inode);
    }
	return WALK_CONT;
}

/* Here are a bunch of callbacks and helpers used to give sk a more filesystem
 * like interface */

/* add this to the list, called by the callback */
void listdent_add_dent(FS_DENT *fs_dent, FS_DATA *fs_data, int flags, struct dentwalk *dentlist) {
    struct dentwalk *p = talloc(dentlist, struct dentwalk);

    p->path = talloc_strndup(p, fs_dent->name, fs_dent->name_max - 1);

    p->type = p->id = 0;
    if(fs_data) {
        p->type = fs_data->type;
        p->id = fs_data->id;
    }

    /* print the data stream name if it exists and is not the default NTFS */
    if ((fs_data) && (((fs_data->type == NTFS_ATYPE_DATA) &&
        (strcmp(fs_data->name, "$Data") != 0)) ||
        ((fs_data->type == NTFS_ATYPE_IDXROOT) &&
        (strcmp(fs_data->name, "$I30") != 0)))) {
        p->path = talloc_asprintf_append(p->path, ":%s", fs_data->name);
    } 

    //if(flags & FS_FLAG_NAME_UNALLOC)
	//    p->path = talloc_asprintf_append(p->path, " (deleted%s)", ((fs_dent->fsi) && (fs_dent->fsi->flags & FS_FLAG_META_ALLOC)) ? "-realloc" : "");

    p->inode = fs_dent->inode;
    p->ent_type = fs_dent->ent_type;
    p->flags = flags;

    list_add_tail(&p->list, &dentlist->list);
}

/* 
 * call back action function for dent_walk
 * This is a based on the callback in fls_lib.c, it adds an entry for each
 * named ADS in NTFS
 */
static uint8_t
listdent_walk_callback_dent(FS_INFO * fs, FS_DENT * fs_dent, int flags, void *ptr) {
    struct dentwalk *dentlist = (struct dentwalk *)ptr;

	/* Make a special case for NTFS so we can identify all of the
	 * alternate data streams!
	 */
    if (((fs->ftype & FSMASK) == NTFS_TYPE) && (fs_dent->fsi)) {

        FS_DATA *fs_data = fs_dent->fsi->attr;
        uint8_t printed = 0;

        while ((fs_data) && (fs_data->flags & FS_DATA_INUSE)) {

            if (fs_data->type == NTFS_ATYPE_DATA) {
                mode_t mode = fs_dent->fsi->mode;
                uint8_t ent_type = fs_dent->ent_type;

                printed = 1;

                /* 
                * A directory can have a Data stream, in which
                * case it would be printed with modes of a
                * directory, although it is really a file
                * So, to avoid confusion we will set the modes
                * to a file so it is printed that way.  The
                * entry for the directory itself will still be
                * printed as a directory
                */

                if ((fs_dent->fsi->mode & FS_INODE_FMT) == FS_INODE_DIR) {

                    /* we don't want to print the ..:blah stream if
                    * the -a flag was not given
                    */
                    if ((fs_dent->name[0] == '.') && (fs_dent->name[1])
                        && (fs_dent->name[2] == '\0')) {
                        fs_data = fs_data->next;
                        continue;
                    }

                    fs_dent->fsi->mode &= ~FS_INODE_FMT;
                    fs_dent->fsi->mode |= FS_INODE_REG;
                    fs_dent->ent_type = FS_DENT_REG;
                }
            
                listdent_add_dent(fs_dent, fs_data, flags, dentlist);

                fs_dent->fsi->mode = mode;
                fs_dent->ent_type = ent_type;
            } else if (fs_data->type == NTFS_ATYPE_IDXROOT) {
                printed = 1;

                /* If it is . or .. only print it if the flags say so,
                 * we continue with other streams though in case the 
                 * directory has a data stream 
                 */
                if (!(ISDOT(fs_dent->name))) 
                    listdent_add_dent(fs_dent, fs_data, flags, dentlist);
            }

            fs_data = fs_data->next;
        }

	    /* A user reported that an allocated file had the standard
	     * attributes, but no $Data.  We should print something */
	    if (printed == 0) {
            listdent_add_dent(fs_dent, fs_data, flags, dentlist);
	    }

    } else {
        /* skip it if it is . or .. and we don't want them */
        if (!(ISDOT(fs_dent->name)))
            listdent_add_dent(fs_dent, NULL, flags, dentlist);
    }
    return WALK_CONT;
}

/* callback function for dent_walk used by listdir */
static uint8_t listdent_walk_callback_list(FS_INFO *fs, FS_DENT *fs_dent, int flags, void *ptr) {
    PyObject *list = (PyObject *)ptr;

    /* we dont want to add '.' and '..' */
    if(ISDOT(fs_dent->name))
        return WALK_CONT;

    PyList_Append(list, PyString_FromString(fs_dent->name));
    return WALK_CONT;
}

/* callback function for file_walk, populates a block list */
static u_int8_t
getblocks_walk_callback(FS_INFO *fs, DADDR_T addr, char *buf, int size, int flags, void *ptr) {

    struct block *b;
    skfile *file = (skfile *) ptr;

    if(size <= 0)
        return WALK_CONT;

    if(flags & FS_FLAG_DATA_RES) {
        /* we have resident ntfs data */
        file->resdata = (char *)talloc_size(NULL, size);
        memcpy(file->resdata, buf, size);
        file->size = size;
    } else {
        /* create a new block entry */
        b = talloc(file->blocks, struct block);
        b->addr = addr;
        b->size = size;
        list_add_tail(&b->list, &file->blocks->list);
        file->size += size;
    }

    /* add to the list */
    return WALK_CONT;
}

/* lookup an inode from a path */
INUM_T lookup_inode(FS_INFO *fs, char *path) {
    INUM_T ret;
    char *tmp = strdup(path);
    /* this is evil and modifies the path! */
    ret = fs_ifind_path_ret(fs, 0, tmp);
    free(tmp);
    return ret;
}

/* callback for lookup_path */
static uint8_t
lookup_path_cb(FS_INFO * fs, FS_DENT * fs_dent, int flags, void *ptr) {
    struct dentwalk *dent = (struct dentwalk *)ptr;

    if (fs_dent->inode == dent->inode) {
        dent->path = talloc_asprintf(dent, "/%s%s", fs_dent->path, fs_dent->name);
        return WALK_STOP;
    }
    return WALK_CONT;
}

/* lookup path for inode, supply an dentwalk ptr (must be a talloc context),
 * name will be filled in */
int lookup_path(FS_INFO *fs, struct dentwalk *dent) {
    int flags = FS_FLAG_NAME_RECURSE | FS_FLAG_NAME_ALLOC;

    /* special case, the walk won't pick this up */
    if(dent->inode == fs->root_inum) {
        dent->path = talloc_strdup(dent, "/");
        return 0;
    }

    /* there is a walk optimised for NTFS */
    if((fs->ftype & FSMASK) == NTFS_TYPE) {
        if(ntfs_find_file(fs, dent->inode, 0, 0, flags, lookup_path_cb, (void *)dent))
            return 1;
    } else {
        if(fs->dent_walk(fs, fs->root_inum, flags, lookup_path_cb, (void *)dent))
            return 1;
    }
    return 0;
}

/* parse an inode string into inode, type, id */
int parse_inode_str(char *str, INUM_T *inode, uint32_t *type, uint32_t *id) {
    char *ptr;

    errno = 0;
    *inode = strtoull(str, &ptr, 10);
    if(errno != 0)
        return 0;

    if(*ptr == '-')
        *type = strtoul(ptr+1, &ptr, 10);
    if(*ptr == '-')
        *id = strtoul(ptr+1, &ptr, 10);

    return 1;
}

/*****************************************************************
 * Now for the python module stuff 
 * ***************************************************************/

/************* SKFS ***************/
static void
skfs_dealloc(skfs *self) {
    if(self->fs)
        self->fs->close(self->fs);
    if(self->img)
        self->img->close(self->img);
    self->ob_type->tp_free((PyObject*)self);
}

static int
skfs_init(skfs *self, PyObject *args, PyObject *kwds) {
    char *imgfile=NULL, *imgtype=NULL, *fstype=NULL;

    static char *kwlist[] = {"imgfile", "imgtype", "fstype", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "s|ss", kwlist, 
				    &imgfile, &imgtype, &fstype))
        return -1; 

    /* force raw to prevent incorrect auto-detection of another imgtype */
    if(!imgtype) {
        imgtype = "raw";
    }

    /* initialise the img and filesystem */
    tsk_error_reset();
    self->img = img_open(imgtype, 1, (const char **)&imgfile);
    if(!self->img) {
      PyErr_Format(PyExc_IOError, "Unable to open image %s: %s", imgfile, tsk_error_str());
      return -1;
    }

    /* initialise the filesystem */
    tsk_error_reset();
    self->fs = fs_open(self->img, 0, fstype);
    if(!self->fs) {
      PyErr_Format(PyExc_RuntimeError, "Unable to open filesystem in image %s: %s", imgfile, tsk_error_str());
      return -1;
    }

    return 0;
}

/* return a list of files and directories */
static PyObject *
skfs_listdir(skfs *self, PyObject *args, PyObject *kwds) {
    PyObject *list;
    char *path=NULL;
    INUM_T inode;
    int flags=0;
    /* these are the boolean options with defaults */
    int alloc=1, unalloc=0;

    static char *kwlist[] = {"path", "alloc", "unalloc", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "s|ii", kwlist, 
                                     &path, &alloc, &unalloc))
        return NULL; 

    tsk_error_reset();
    inode = lookup_inode(self->fs, path);
    if(inode == 0)
        return PyErr_Format(PyExc_IOError, "Unable to find inode for path %s: %s", path, tsk_error_str());

    /* set flags */
    if(alloc)
        flags |= FS_FLAG_NAME_ALLOC;
    if(unalloc)
        flags |= FS_FLAG_NAME_UNALLOC;

    list = PyList_New(0);

    tsk_error_reset();
    self->fs->dent_walk(self->fs, inode, flags, listdent_walk_callback_list, (void *)list);
    if(tsk_errno) {
      return PyErr_Format(PyExc_IOError, "Unable to list inode %lu: %s", (ULONG)inode, tsk_error_str());
    };

    return list;
}

/* Open a file from the skfs */
static PyObject *
skfs_open(skfs *self, PyObject *args, PyObject *kwds) {
    char *path=NULL;
    PyObject *inode=NULL;
    int ret;
    PyObject *fileargs, *filekwds;
    skfile *file;

    static char *kwlist[] = {"path", "inode", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "|sO", kwlist, &path, &inode))
        return NULL; 

    /* make sure we at least have a path or inode */
    if(path==NULL && inode==NULL)
        return PyErr_Format(PyExc_SyntaxError, "One of path or inode must be specified");

    /* create an skfile object and return it */
    fileargs = PyTuple_New(0);
    if(path)
        filekwds = Py_BuildValue("{sOsssO}", "filesystem", (PyObject *)self, 
                                 "path", path, "inode", inode);
    else
        filekwds = Py_BuildValue("{sOsO}", "filesystem", (PyObject *)self, 
                                 "inode", inode);

    file = PyObject_New(skfile, &skfileType);
    ret = skfile_init(file, fileargs, filekwds);
    Py_DECREF(fileargs);
    Py_DECREF(filekwds);

    if(ret == -1) return NULL;
    return (PyObject *)file;
}

/* perform a filesystem walk (like os.walk) */
static PyObject *
skfs_walk(skfs *self, PyObject *args, PyObject *kwds) {
    char *path=NULL;
    int alloc=1, unalloc=0, ret;
    int names=1, inodes=0;
    PyObject *fileargs, *filekwds;
    skfs_walkiter *iter;
    INUM_T inode=0;

    static char *kwlist[] = {"path", "inode", "alloc", "unalloc", "names", "inodes", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "|sKiiii", kwlist, &path, &inode, 
                                    &alloc, &unalloc, &names, &inodes))
        return NULL; 

    /* create an skfs_walkiter object to return to the caller */
    fileargs = PyTuple_New(0);
    if(path)
        filekwds = Py_BuildValue("{sOsssKsisisisi}", "filesystem", (PyObject *)self, "path", path,
                                 "inode", inode, "alloc", alloc, "unalloc", unalloc, "names", names, "inodes", inodes);
    else
        filekwds = Py_BuildValue("{sOsKsisisisi}", "filesystem", (PyObject *)self, 
                                 "inode", inode, "alloc", alloc, "unalloc", unalloc, "names", names, "inodes", inodes);

    iter = PyObject_New(skfs_walkiter, &skfs_walkiterType);
    ret = skfs_walkiter_init(iter, fileargs, filekwds);
    Py_DECREF(fileargs);
    Py_DECREF(filekwds);

    if(ret == -1) return NULL;
    return (PyObject *)iter;
}

/* perform an inode walk, return a list of inodes. This is best only used to
 * find unallocated (deleted) inodes as it builds a list in memory and returns
 * it (skfs.walk by contrast uses a generator). */
static PyObject *
skfs_iwalk(skfs *self, PyObject *args, PyObject *kwds) {
    int alloc=0, unalloc=1, flags=0;
    PyObject *fileargs, *filekwds;
    PyObject *list;
    INUM_T inode=0;

    static char *kwlist[] = {"inode", "alloc", "unalloc", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "K|ii", kwlist, &inode, 
                                    &alloc, &unalloc))
        return NULL; 

    // ignore args for now and just do full walk (start->end)
    // flags are set to find all unlinked files (alloc or unalloc)
    // and only used inodes.
    flags = FS_FLAG_META_UNLINK;
        
    list = PyList_New(0);
    self->fs->inode_walk(self->fs, self->fs->first_inum, self->fs->last_inum, flags, 
            (FS_INODE_WALK_FN) inode_walk_callback, (void *)list);

    return list;
}

/* stat a file */
static PyObject *
skfs_stat(skfs *self, PyObject *args, PyObject *kwds) {
    PyObject *result;
    PyObject *os, *inode_obj;
    char *path=NULL;
    INUM_T inode=0;
    FS_INODE *fs_inode;
    int type=0, id=0;

    static char *kwlist[] = {"path", "inode", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "|sO", kwlist, &path, &inode_obj))
        return NULL; 

    /* make sure we at least have a path or inode */
    if(path==NULL && inode_obj==NULL)
        return PyErr_Format(PyExc_SyntaxError, "One of path or inode must be specified");

    if(path) {
        tsk_error_reset();
        inode = lookup_inode(self->fs, path);
        if(inode == 0)
            return PyErr_Format(PyExc_IOError, "Unable to find inode for path %s: %d: %s", path, (ULONG) inode, tsk_error_str());
    } else {
        /* inode can be an int or a string */
        if(PyNumber_Check(inode_obj)) {
            PyObject *l = PyNumber_Long(inode_obj);
            inode = PyLong_AsUnsignedLongLong(l);
            Py_DECREF(l);
        } else {
            if(!parse_inode_str(PyString_AsString(inode_obj), &inode, &type, &id))
                return PyErr_Format(PyExc_IOError, "Inode must be a long or a string of the format \"inode[-type-id]\"");
        }
    }

    /* can we lookup this inode? */
    tsk_error_reset();
    fs_inode = self->fs->inode_lookup(self->fs, inode);
    if(fs_inode == NULL)
        return PyErr_Format(PyExc_IOError, "Unable to find inode %lu: %s", (ULONG)inode, tsk_error_str());

    /* return a real stat_result! */
    /* (mode, ino, dev, nlink, uid, gid, size, atime, mtime, ctime) */
    os = PyImport_ImportModule("os");
    result = PyObject_CallMethod(os, "stat_result", "((iiliiiiiii))", 
                                 fs_inode->mode, fs_inode->addr, 0, fs_inode->nlink, 
                                 fs_inode->uid, fs_inode->gid, fs_inode->size,
                                 fs_inode->atime, fs_inode->mtime, fs_inode->ctime);
    Py_DECREF(os);
    
    /* release the fs_inode */
    fs_inode_free(fs_inode);
    return result;
}

/* stat an already open skfile */
static PyObject *
skfs_fstat(skfs *self, PyObject *args) {
    PyObject *result, *skfile_obj, *os;
    FS_INODE *fs_inode;

    if(!PyArg_ParseTuple(args, "O", &skfile_obj))
        return NULL; 

    /* check the type of the file object */
    if(PyObject_TypeCheck(skfile_obj, &skfileType) == 0) {
        PyErr_Format(PyExc_TypeError, "file is not an skfile instance");
        return NULL;
    }

    fs_inode = ((skfile *)skfile_obj)->fs_inode;
    
    /* (mode, ino, dev, nlink, uid, gid, size, atime, mtime, ctime) */
    os = PyImport_ImportModule("os");
    result = PyObject_CallMethod(os, "stat_result", "((iiliiiiiii))", 
                                 fs_inode->mode, fs_inode->addr, 0, fs_inode->nlink, 
                                 fs_inode->uid, fs_inode->gid, fs_inode->size,
                                 fs_inode->atime, fs_inode->mtime, fs_inode->ctime);
    Py_DECREF(os);
    return result;
}

/* this new object is requred to support the iterator protocol for skfs.walk
 * */
static void 
skfs_walkiter_dealloc(skfs_walkiter *self) {
    Py_XDECREF(self->skfs);
    talloc_free(self->walklist);
    self->ob_type->tp_free((PyObject*)self);
}

static int 
skfs_walkiter_init(skfs_walkiter *self, PyObject *args, PyObject *kwds) {
    char *path=NULL;
    PyObject *skfs_obj;
    struct dentwalk *root;
    int alloc=1, unalloc=0;
    int names=1, inodes=0;
    INUM_T inode;

    static char *kwlist[] = {"filesystem", "path", "inode", "alloc", "unalloc", "names", "inodes", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "O|sKiiii", kwlist, &skfs_obj, 
                                    &path, &inode, &alloc, &unalloc, &names, &inodes))
        return -1; 

    /* must have at least inode or path */
    if(inode == 0 && path == NULL) {
        PyErr_Format(PyExc_SyntaxError, "One of filename or inode must be specified");
        return -1;
    };

    /* set flags */
    self->flags = self->myflags = 0;
    if(alloc)
        self->flags |= FS_FLAG_NAME_ALLOC;
    if(unalloc)
        self->flags |= FS_FLAG_NAME_UNALLOC;
    if(names)
        self->myflags |= SK_FLAG_NAMES;
    if(inodes)
        self->myflags |= SK_FLAG_INODES;

    /* incref the skfs */
    Py_INCREF(skfs_obj);
    self->skfs = (skfs *)skfs_obj;

    /* Initialise the path stack */
    self->walklist = talloc(NULL, struct dentwalk);
    INIT_LIST_HEAD(&self->walklist->list);

    /* add the start path */
    root = talloc(self->walklist, struct dentwalk);
    root->type = root->id = 0;
    root->flags = FS_FLAG_NAME_ALLOC;

    if(inode == 0) {
        tsk_error_reset();
        root->inode = lookup_inode(self->skfs->fs, path);
    } else root->inode = inode;

    if(path == NULL) {
        tsk_error_reset();
        lookup_path(self->skfs->fs, root);
    } else root->path = talloc_strdup(root, path);

    list_add(&root->list, &self->walklist->list);

    return 0;
}

static PyObject *skfs_walkiter_iternext(skfs_walkiter *self) {
    PyObject *dirlist, *filelist, *root, *result, *inode;
    struct dentwalk *dw, *dwlist;
    struct dentwalk *dwtmp, *dwtmp2;
    char *tmp;
    int i;

    /* are we done ? */
    if(list_empty(&self->walklist->list))
        return NULL;

    /* pop an item from the stack */
    list_next(dw, &self->walklist->list, list);

    /* initialise our list for this walk */
    dwlist = talloc(self->walklist, struct dentwalk);
    INIT_LIST_HEAD(&dwlist->list);

    /* walk this directory */
    tsk_error_reset();
    self->skfs->fs->dent_walk(self->skfs->fs, dw->inode, self->flags, 
                              listdent_walk_callback_dent, (void *)dwlist);
    if(tsk_errno) {
        PyErr_Format(PyExc_IOError, "Walk error: %s", tsk_error_str());
        talloc_free(dwlist);
        return NULL;
    }

    /* process the list */
    dirlist = PyList_New(0);
    filelist = PyList_New(0);
    list_for_each_entry_safe(dwtmp, dwtmp2, &dwlist->list, list) {

        PyObject *inode_val, *name_val, *inode_name_val;
        
        /* build all the objects */
        inode_val = (PyObject *)PyObject_New(skfs_inode, &skfs_inodeType);
        ((skfs_inode *)inode_val)->inode = dwtmp->inode;
        ((skfs_inode *)inode_val)->type = dwtmp->type;
        ((skfs_inode *)inode_val)->id = dwtmp->id;
        ((skfs_inode *)inode_val)->alloc = (dwtmp->flags & FS_FLAG_NAME_ALLOC) ? 1 : 0;

        name_val = PyString_FromString(dwtmp->path);
        inode_name_val = Py_BuildValue("(OO)", inode_val, name_val);

        /* process directories */
        if(dwtmp->ent_type & FS_DENT_DIR) {

            /* place into dirlist */
            if((self->myflags & SK_FLAG_INODES) && (self->myflags & SK_FLAG_NAMES))
                PyList_Append(dirlist, inode_name_val);
            else if(self->myflags & SK_FLAG_INODES)
                PyList_Append(dirlist, inode_val);
            else if(self->myflags & SK_FLAG_NAMES)
                PyList_Append(dirlist, name_val);

            /* steal it and push onto the directory stack */
            if(dwtmp->flags & FS_FLAG_NAME_ALLOC) {
                talloc_steal(self->walklist, dwtmp);
                tmp = dwtmp->path;
                if(strcmp(dw->path, "/") == 0)
                    dwtmp->path = talloc_asprintf(dwtmp, "/%s", tmp);
                else
                    dwtmp->path = talloc_asprintf(dwtmp, "%s/%s", dw->path, tmp);
                talloc_free(tmp);
                list_move(&dwtmp->list, &self->walklist->list);
            }

        } else {
            /* place into filelist */
             if((self->myflags & SK_FLAG_INODES) && (self->myflags & SK_FLAG_NAMES))
                PyList_Append(filelist, inode_name_val);
            else if(self->myflags & SK_FLAG_INODES)
                PyList_Append(filelist, inode_val);
            else if(self->myflags & SK_FLAG_NAMES)
                PyList_Append(filelist, name_val);
        }

        Py_DECREF(inode_name_val);
        Py_DECREF(name_val);
        Py_DECREF(inode_val);
    }

    /* now build root */
    inode = (PyObject *)PyObject_New(skfs_inode, &skfs_inodeType);
    ((skfs_inode *)inode)->inode = dw->inode;
    ((skfs_inode *)inode)->type = dw->type;
    ((skfs_inode *)inode)->id = dw->id;
    ((skfs_inode *)inode)->alloc = (dw->flags & FS_FLAG_NAME_ALLOC) ? 1 : 0;

    if((self->myflags & SK_FLAG_INODES) && (self->myflags & SK_FLAG_NAMES))
        root = Py_BuildValue("(Ns)", inode, dw->path);
    else if(self->myflags & SK_FLAG_INODES)
        root = inode;
    else if(self->myflags & SK_FLAG_NAMES)
        root = PyString_FromString(dw->path);
    else {
        Py_INCREF(Py_None);
        root = Py_None;
    }

    result = Py_BuildValue("(NNN)", root, dirlist, filelist);

    /* now delete this entry from the stack */
    list_del(&dw->list);
    talloc_free(dw);
    talloc_free(dwlist);

    return result;
}

/************** SKFS_INODE **********/
static int skfs_inode_init(skfs_inode *self, PyObject *args, PyObject *kwds) {
    static char *kwlist[] = {"inode", "type", "id", "alloc", NULL};
    int alloc;

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "KiiO", kwlist, 
                                    &self->inode, &self->type, &self->id, alloc))
        return -1; 
    self->alloc = alloc ? 1 : 0;
    return 0;
}

static PyObject *skfs_inode_str(skfs_inode *self) {
    PyObject *result;
    char *str;
    str = talloc_asprintf(NULL, "%llu-%u-%u", self->inode, self->type, self->id);
    result = PyString_FromString(str);
    talloc_free(str);
    return result;
}

static PyObject *
skfs_inode_getinode(skfs_inode *self, void *closure) {
    return PyLong_FromUnsignedLongLong(self->inode);
}

static PyObject *
skfs_inode_getalloc(skfs_inode *self, void *closure) {
    if(self->alloc)
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

static PyObject *skfs_inode_long(skfs_inode *self) {
    return PyLong_FromUnsignedLongLong(self->inode);
}

/************* SKFILE ***************/
static void
skfile_dealloc(skfile *self) {
    Py_XDECREF(self->skfs);
    talloc_free(self->blocks);
    if(self->resdata)
        talloc_free(self->resdata);
    fs_inode_free(self->fs_inode);
    self->ob_type->tp_free((PyObject*)self);
}

static int
skfile_init(skfile *self, PyObject *args, PyObject *kwds) {
    char *filename=NULL;
    PyObject *inode_obj;
    INUM_T inode=0;
    PyObject *skfs_obj;
    FS_INFO *fs;
    int flags;

    self->type = 0;
    self->id = 0;

    static char *kwlist[] = {"filesystem", "path", "inode", NULL};

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "O|sO", kwlist, 
                                    &skfs_obj, &filename, &inode_obj))
        return -1; 

    /* check the type of the filesystem object */
    if(PyObject_TypeCheck(skfs_obj, &skfsType) == 0) {
        PyErr_Format(PyExc_TypeError, "filesystem is not an skfs instance");
        return -1;
    }

    fs = ((skfs *)skfs_obj)->fs;

    /* must specify either inode or filename */
    if(filename==NULL && inode_obj==NULL) {
        PyErr_Format(PyExc_SyntaxError, "One of filename or inode must be specified");
        return -1;
    };

    if(filename) {
        tsk_error_reset();
        inode = lookup_inode(fs, filename);
        if(inode == 0) {
            PyErr_Format(PyExc_IOError, "Unable to find inode for file %s: %s", filename, tsk_error_str());
            return -1;
        }
    } else {
        /* inode can be an int or a string */
        if(PyNumber_Check(inode_obj)) {
            PyObject *l = PyNumber_Long(inode_obj);
            inode = PyLong_AsUnsignedLongLong(l);
            Py_DECREF(l);
        } else {
            if(!parse_inode_str(PyString_AsString(inode_obj), &inode, &self->type, &self->id)) {
                PyErr_Format(PyExc_IOError, "Inode must be a long or a string of the format \"inode[-type-id]\"");
                return -1;
            }
        }
    }

    /* can we lookup this inode? */
    tsk_error_reset();
    self->fs_inode = fs->inode_lookup(fs, inode);
    if(self->fs_inode == NULL) {
        PyErr_Format(PyExc_IOError, "Unable to find inode: %s", tsk_error_str());
        return -1;
    };

    /* store a ref to the skfs */
    Py_INCREF(skfs_obj);
    self->skfs = skfs_obj;

    self->resdata = NULL;
    self->readptr = 0;
    self->size = 0;

    /* perform a file run and populate the block list, use type and id to
     * ensure we follow the correct attribute for NTFS (these default to 0
     * which will give us the default data attribute). size will also be set
     * during the walk */

    flags = FS_FLAG_FILE_AONLY | FS_FLAG_FILE_RECOVER | FS_FLAG_FILE_NOSPARSE;
    if(self->id == 0)
        flags |= FS_FLAG_FILE_NOID;

    self->blocks = talloc(NULL, struct block);
    INIT_LIST_HEAD(&self->blocks->list);
    tsk_error_reset();
    fs->file_walk(fs, self->fs_inode, self->type, self->id, flags,
                 (FS_FILE_WALK_FN) getblocks_walk_callback, (void *)self);
    if(tsk_errno) {
        PyErr_Format(PyExc_IOError, "Error reading inode: %s", tsk_error_str());
        return -1;
    };

    return 0;
}

static PyObject *
skfile_str(skfile *self) {
    PyObject *result;
    char *str;
    str = talloc_asprintf(NULL, "%llu-%u-%u", self->fs_inode->addr, self->type, self->id);
    result = PyString_FromString(str);
    talloc_free(str);
    return result;
}

static PyObject *
skfile_read(skfile *self, PyObject *args) {
    char *buf;
    int cur, written;
    PyObject *retdata;
    FS_INFO *fs;
    struct block *b;
    int readlen=-1;

    fs = ((skfs *)self->skfs)->fs;

    if(!PyArg_ParseTuple(args, "|i", &readlen))
        return NULL; 

    /* adjust readlen if size not given or is too big */
    if(readlen < 0 || self->readptr + readlen > self->size)
        readlen = self->size - self->readptr;

    /* special case for NTFS resident data */
    if(self->resdata) {
         retdata = PyString_FromStringAndSize(self->resdata + self->readptr, readlen);
         self->readptr += readlen;
         return retdata;
    }
    
    /* allocate buf, be generous in case data straddles blocks */
    buf = (char *)malloc(readlen + (2 * fs->block_size));
    if(!buf)
        return PyErr_Format(PyExc_MemoryError, "Out of Memory allocating read buffer.");

    /* read necessary blocks into buf */
    cur = written = 0;
    list_for_each_entry(b, &self->blocks->list, list) {

        /* we don't need any data in this block, skip */
        if(cur + fs->block_size <= self->readptr) {
            cur += fs->block_size;
            continue;
        }

        /* read block into buf */
        fs_read_block_nobuf(fs, buf+written, fs->block_size, b->addr);
        cur += fs->block_size;
        written += fs->block_size;

        /* are we done yet? */
        if(cur >= self->readptr + readlen)
            break;
    }

    /* copy what we want into the return string */
    retdata = PyString_FromStringAndSize(buf + (self->readptr % fs->block_size), readlen);
    free(buf);

    self->readptr += readlen;
    return retdata;
}

static PyObject *
skfile_seek(skfile *self, PyObject *args) {
    int offset=0;
    int whence=0;

    if(!PyArg_ParseTuple(args, "i|i", &offset, &whence))
        return NULL; 

    switch(whence) {
        case 0:
            self->readptr = offset;
            break;
        case 1:
            self->readptr += offset;
            break;
        case 2:
            self->readptr = self->size + offset;
            break;
        default:
            return PyErr_Format(PyExc_IOError, "Invalid argument (whence): %d", whence);
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
skfile_tell(skfile *self) {
    return PyLong_FromLongLong(self->readptr);
}

static PyObject *
skfile_blocks(skfile *self) {
    struct block *b;

    PyObject *list = PyList_New(0);
    list_for_each_entry(b, &self->blocks->list, list) {
        PyList_Append(list, PyLong_FromUnsignedLongLong(b->addr));
    }
    return list;
}

/* these are the module methods */
static PyMethodDef sk_methods[] = {
    {NULL}  /* Sentinel */
};

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
initsk(void) 
{
    PyObject* m;

    /* create module */
    m = Py_InitModule3("sk", sk_methods,
                       "Sleuthkit module.");

    /* setup skfs type */
    skfsType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&skfsType) < 0)
        return;

    Py_INCREF(&skfsType);
    PyModule_AddObject(m, "skfs", (PyObject *)&skfsType);

    /* setup skfs_walkiter type */
    skfs_walkiterType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&skfs_walkiterType) < 0)
        return;

    Py_INCREF(&skfs_walkiterType);

    /* setup inode type */
    skfs_inodeType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&skfs_inodeType) < 0)
        return;

    Py_INCREF(&skfs_inodeType);
    PyModule_AddObject(m, "skinode", (PyObject *)&skfs_inodeType);

    /* setup skfile type */
    skfileType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&skfileType) < 0)
        return;

    Py_INCREF(&skfileType);
    PyModule_AddObject(m, "skfile", (PyObject *)&skfileType);
}