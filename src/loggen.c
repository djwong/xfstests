/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
 
/*
 *
 * loggen: Generate log entries. Very much incomplete. The empty log
 *         record is a bit of a misnomer since we need to jump through
 *         hoops to get a log record that parses ok yet does nothing.
 *
 *                                                  - dxm 29/09/00
 */

#include <xfs/libxfs.h>
#include <xfs/xfs_log.h>
#include <xfs/xfs_log_priv.h>

#ifndef ASSIGN_ANY_LSN_DISK
#define ASSIGN_ANY_LSN_DISK(lsn,cycle,block)  \
    { \
	INT_SET(((uint *)&(lsn))[0], ARCH_CONVERT, (cycle)); \
	INT_SET(((uint *)&(lsn))[1], ARCH_CONVERT, (block)); \
    }
#endif

void
usage(void)
{
    fprintf(stderr,"Usage: loggen\n"
                   "           set up parameters before writing record(s):\n"
                   "               -f f     - set format\n"
                   "               -u u     - set uuid\n"
                   "               -c c     - set cycle\n"
                   "               -b b     - set block\n"
                   "               -C c     - set tail cycle\n"
                   "               -B b     - set tail block\n"
                   "           write log record(s):\n"
                   "               -z n     - write n zero block(s)     (1BB)\n"
                   "               -e n     - write n empty record(s)   (2BB)\n"
                   "               -m n     - write n unmount record(s) (2BB)\n"
                   "\n"
                   "            redirect stdout to external log partition, or pipe to\n"
                   "            dd with appropriate parameters to stuff into internal log.\n"
    );
    exit(1);
}

int         bufblocks            = 0;
void        *buf                 = NULL;
int         param_cycle          = 1;
int         param_block          = 0;
int         param_tail_cycle     = 1;
int         param_tail_block     = 0;
int         param_fmt            = XLOG_FMT;
uuid_t      param_uuid           = {0};

void
loggen_alloc(int blocks)
{
    if (!(buf=realloc(buf, blocks*BBSIZE))) {
        fprintf(stderr,"failed to allocate %d block(s)\n", blocks);
        exit(1);
    }
    memset(buf, 0, blocks*BBSIZE);
    bufblocks=blocks;
}

void
loggen_write(void)
{         
    if (!buf) {
        fprintf(stderr,"no buffer allocated\n");
        exit(1);
    }

    if (fwrite(buf, BBSIZE, bufblocks, stdout) != bufblocks) {
        perror("fwrite");
        exit(1);
    }

}

void
loggen_zero(int count)
{
    if (!count) count=1;
    
    fprintf(stderr,"   *** zero block (1BB) x %d\n", count);
    loggen_alloc(1);
    while (count--)
        loggen_write();
}      
      
void
loggen_unmount(int count)
{
    xlog_rec_header_t       *head;
    xlog_op_header_t        *op;
    /* the data section must be 32 bit size aligned */
    struct {
        __uint16_t magic;
        __uint16_t pad1;
        __uint32_t pad2; /* may as well make it 64 bits */
    } magic = { XLOG_UNMOUNT_TYPE, 0, 0 };
    
    if (!count) count=1;
    
    fprintf(stderr,"   *** unmount record (2BB) x %d\n", count);
    loggen_alloc(2);
    
    head = (xlog_rec_header_t *)buf;
    op   = (xlog_op_header_t  *)(((char*)buf)+BBSIZE);

    /* note that oh_tid actually contains the cycle number
     * and the tid is stored in h_cycle_data[0] - that's the
     * way things end up on disk.
     */

    INT_SET(head->h_magicno,        ARCH_CONVERT, XLOG_HEADER_MAGIC_NUM);
    INT_SET(head->h_cycle,          ARCH_CONVERT, param_cycle);
    INT_SET(head->h_version,        ARCH_CONVERT, 1);
    INT_SET(head->h_len,            ARCH_CONVERT, 20);
    INT_SET(head->h_chksum,         ARCH_CONVERT, 0);
    INT_SET(head->h_prev_block,     ARCH_CONVERT, -1);
    INT_SET(head->h_num_logops,     ARCH_CONVERT, 1);
    INT_SET(head->h_cycle_data[0],  ARCH_CONVERT, 0xb0c0d0d0);
    INT_SET(head->h_fmt,            ARCH_CONVERT, param_fmt);
    
    ASSIGN_ANY_LSN_DISK(head->h_tail_lsn, param_tail_cycle, param_tail_block);

    memcpy(head->h_fs_uuid,  param_uuid, sizeof(uuid_t));

    /* now a log unmount op */
    INT_SET(op->oh_tid,             ARCH_CONVERT, param_cycle);
    INT_SET(op->oh_len,             ARCH_CONVERT, sizeof(magic));
    INT_SET(op->oh_clientid,        ARCH_CONVERT, XFS_LOG);
    INT_SET(op->oh_flags,           ARCH_CONVERT, XLOG_UNMOUNT_TRANS);
    INT_SET(op->oh_res2,            ARCH_CONVERT, 0);

    /* and the data for this op */

    memcpy(op+1, &magic, sizeof(magic));
    
    while (count--) {
        ASSIGN_ANY_LSN_DISK(head->h_lsn,         
                param_cycle, param_block++);
        
        loggen_write();
    }
} 
  
void
loggen_empty(int count)
{
    xlog_rec_header_t       *head;
    xlog_op_header_t        *op1, *op2, *op3, *op4, *op5;
    xfs_trans_header_t      *trans;
    xfs_buf_log_format_t    blfs;
    xfs_buf_log_format_t    *blf;
    int                     *data;
    char                    *p;
    
    if (!count) count=1;
    
    fprintf(stderr,"   *** empty record (2BB) x %d\n", count);
    loggen_alloc(2);
    
    p=(char*)buf;
    head  = (xlog_rec_header_t   *)p;         p+=BBSIZE;
    op1   = (xlog_op_header_t    *)p;         p+=sizeof(xlog_op_header_t);
    op2   = (xlog_op_header_t    *)p;         p+=sizeof(xlog_op_header_t);
    trans = (xfs_trans_header_t  *)p;         p+=sizeof(xfs_trans_header_t);
    op3   = (xlog_op_header_t    *)p;         p+=sizeof(xlog_op_header_t);
    blf   = (xfs_buf_log_format_t*)p;         p+=sizeof(xfs_buf_log_format_t);
    op4   = (xlog_op_header_t    *)p;         p+=sizeof(xlog_op_header_t);
    data  = (int                 *)p;         p+=sizeof(int);
    op5   = (xlog_op_header_t    *)p;         p+=sizeof(xlog_op_header_t);

    /* note that oh_tid actually contains the cycle number
     * and the tid is stored in h_cycle_data[0] - that's the
     * way things end up on disk.
     */

    INT_SET(head->h_magicno,        ARCH_CONVERT, XLOG_HEADER_MAGIC_NUM);
    INT_SET(head->h_cycle,          ARCH_CONVERT, param_cycle);
    INT_SET(head->h_version,        ARCH_CONVERT, 1);
    INT_SET(head->h_len,            ARCH_CONVERT, 5*sizeof(xlog_op_header_t) +
                                                    sizeof(xfs_trans_header_t)+
                                                    sizeof(xfs_buf_log_format_t)+
                                                    sizeof(int));
    INT_SET(head->h_chksum,         ARCH_CONVERT, 0);
    INT_SET(head->h_prev_block,     ARCH_CONVERT, -1);
    INT_SET(head->h_num_logops,     ARCH_CONVERT, 5);
    INT_SET(head->h_cycle_data[0],  ARCH_CONVERT, 0xb0c0d0d0);
    INT_SET(head->h_fmt,            ARCH_CONVERT, param_fmt);
    
    ASSIGN_ANY_LSN_DISK(head->h_tail_lsn,    
            param_tail_cycle, param_tail_block);

    memcpy(head->h_fs_uuid,  param_uuid, sizeof(uuid_t));

    /* start */
    INT_SET(op1->oh_tid,            ARCH_CONVERT, 1);
    INT_SET(op1->oh_len,            ARCH_CONVERT, 0);
    INT_SET(op1->oh_clientid,       ARCH_CONVERT, XFS_TRANSACTION);
    INT_SET(op1->oh_flags,          ARCH_CONVERT, XLOG_START_TRANS);
    INT_SET(op1->oh_res2,           ARCH_CONVERT, 0);
    /* dummy */
    INT_SET(op2->oh_tid,            ARCH_CONVERT, 0xb0c0d0d0);
    INT_SET(op2->oh_len,            ARCH_CONVERT, sizeof(xfs_trans_header_t));
    INT_SET(op2->oh_clientid,       ARCH_CONVERT, XFS_TRANSACTION);
    INT_SET(op2->oh_flags,          ARCH_CONVERT, 0);
    INT_SET(op2->oh_res2,           ARCH_CONVERT, 0);
    /* dummy transaction - this stuff doesn't get endian converted */
    trans->th_magic     = XFS_TRANS_MAGIC;
    trans->th_type      = XFS_TRANS_DUMMY1;
    trans->th_tid       = 0;
    trans->th_num_items = 1;
    /* buffer */
    INT_SET(op3->oh_tid,            ARCH_CONVERT, 0xb0c0d0d0);
    INT_SET(op3->oh_len,            ARCH_CONVERT, sizeof(xfs_buf_log_format_t));
    INT_SET(op3->oh_clientid,       ARCH_CONVERT, XFS_TRANSACTION);
    INT_SET(op3->oh_flags,          ARCH_CONVERT, 0);
    INT_SET(op3->oh_res2,           ARCH_CONVERT, 0);
    /* an empty buffer too */
    blfs.blf_type       = XFS_LI_BUF;
    blfs.blf_size       = 2;
    blfs.blf_flags      = XFS_BLI_CANCEL;
    blfs.blf_blkno      = 1;
    blfs.blf_map_size   = 1;
    blfs.blf_data_map[0]= 0;
    memcpy(blf, &blfs, sizeof(blfs));
    /* commit */
    INT_SET(op4->oh_tid,            ARCH_CONVERT, 0xb0c0d0d0);
    INT_SET(op4->oh_len,            ARCH_CONVERT, sizeof(int));
    INT_SET(op4->oh_clientid,       ARCH_CONVERT, XFS_TRANSACTION);
    INT_SET(op4->oh_flags,          ARCH_CONVERT, 0);
    INT_SET(op4->oh_res2,           ARCH_CONVERT, 0);
    /* and the data */
    *data=*(int*)(char*)"FISH"; /* this won't get written (I hope) */
    /* commit */
    INT_SET(op5->oh_tid,            ARCH_CONVERT, 0xb0c0d0d0);
    INT_SET(op5->oh_len,            ARCH_CONVERT, 0);
    INT_SET(op5->oh_clientid,       ARCH_CONVERT, XFS_TRANSACTION);
    INT_SET(op5->oh_flags,          ARCH_CONVERT, XLOG_COMMIT_TRANS);
    INT_SET(op5->oh_res2,           ARCH_CONVERT, 0);

    while (count--) {
        ASSIGN_ANY_LSN_DISK(head->h_lsn,         
                param_cycle, param_block++);
        
        loggen_write();
    }
}   

int
main(int argc, char *argv[])
{
    int c;
    
    fprintf(stderr,"*** loggen\n");
    
    if (argc<2) usage();
    
    while ((c = getopt(argc, argv, "f:u:c:b:C:B:z:e:m:")) != -1) {
	switch (c) {
	    case 'f':
                param_fmt=atoi(optarg);
                break;
	    case 'u':
                memset(param_uuid, atoi(optarg), sizeof(param_uuid));
                break;
	    case 'c':
                param_cycle=atoi(optarg);
                break;
	    case 'b':
                param_block=atoi(optarg);
                break;
	    case 'C':
                param_tail_cycle=atoi(optarg);
                break;
	    case 'B':
                param_tail_block=atoi(optarg);
                break;
                
	    case 'z':
                loggen_zero(atoi(optarg));
                break;
	    case 'e':
                loggen_empty(atoi(optarg));
                break;
	    case 'm':
                loggen_unmount(atoi(optarg));
                break;
                
	    default:
		fprintf(stderr, "unknown option\n");
                usage();
	}
    }
    return 0;   
}
