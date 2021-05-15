#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <fts.h>
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>

#include <errno.h>
#include <err.h>

#include "ls.h"

/*extern area*/
extern int optind;

/*global variable*/
unsigned int g_totalBlockNum;
unsigned long gl_totalSizeNum;
blksize_t g_blocksize;

int (*comp)(const FTSENT **a, const FTSENT **b);


void childrenProcess(FTSENT *parent,unsigned int para_flag){
    if (parent != NULL) {
        if (IS_PARAM_RECURSIVE(para_flag)) {
            if (strcmp(parent->fts_path,".") !=0){
                printf("\n%s:\n", parent->fts_path);
            }
        }
        if ((IS_PARAM_BLOCKSIZE(para_flag) ||
             IS_PARAM_LIST(para_flag) ||
             IS_PARAM_LIST_WITH_NUMERICALLY(para_flag))
            && isatty(STDOUT_FILENO)
            && (g_totalBlockNum != 0)) {
            printf("total ");
            if (IS_PARAM_HUMANREADABLE(para_flag))
                print_human_size(gl_totalSizeNum);
            else if (IS_PARAM_KILOBYTE(para_flag))
                printf("%d ", g_totalBlockNum * g_blocksize / 1024);
            else
                printf("%d ", g_totalBlockNum);
            printf("\n");
        }
    }

    if (fe_getListSize() == 0)
        return;
    fe_iterateFiles(para_flag);
    return;
}

void childrenPreProcess(FTSENT *pFtsent, unsigned int para_flag){
    char uname[MAX_USER_NAME];
    char gname[MAX_GROUP_NAME];
    struct stat *pStat = pFtsent->fts_statp;
    struct passwd   *pPasswd;
    struct group    *pGroup;
    int unlen, gnlen;

    if (IS_PARAM_BLOCKSIZE(para_flag) ||
        IS_PARAM_LIST(para_flag)||
        IS_PARAM_LIST_WITH_NUMERICALLY(para_flag)) {
        g_totalBlockNum += pStat->st_blocks;
        gl_totalSizeNum += pStat->st_size;
    }

    if ( IS_PARAM_LIST(para_flag)||
            IS_PARAM_LIST_WITH_NUMERICALLY(para_flag)) {
        errno = 0;
        pPasswd = getpwuid(pStat->st_uid);
        if (pPasswd == NULL && errno != 0) {
            print_err("ls: getpwuid error %s\n", strerror(errno));
        }
        if (pPasswd == NULL || pPasswd->pw_name == NULL || IS_PARAM_LIST_WITH_NUMERICALLY(para_flag)) {
            if ((unlen = snprintf(uname, 20, "%u", pStat->st_uid)) < 0)
                print_err("ls: snprintf error %s\n", strerror(errno));
        } else if ((unlen = snprintf(uname, 20, "%s", pPasswd->pw_name)) < 0) {
            print_err("ls: snprintf error %s\n",  strerror(errno));
        }
        errno = 0;
        pGroup = getgrgid(pFtsent->fts_statp->st_gid);
        if (pGroup == NULL && errno != 0) {
            print_err("ls: getgrgid error %s\n", strerror(errno));
        }
        if (pGroup == NULL || pGroup->gr_name == NULL || IS_PARAM_LIST_WITH_NUMERICALLY(para_flag)) {
            if ((gnlen = snprintf(gname, 20, "%u", pStat->st_gid)) < 0)
                print_err("ls: snprintf error %s\n",  strerror(errno));
        } else if ((gnlen = snprintf(gname, 20, "%s", pGroup->gr_name)) < 0) {
            print_err("ls: snprintf error %s\n",  strerror(errno));
        }
    }
    if(fe_makeFileEntity(pFtsent, uname, gname)!=0)
        fprintf(stderr,"ls: pre process file fail\n");
    return;
}

int file_ls(FTS* pfts, FTSENT* parent, unsigned int para_flag)
{
    FTSENT * ptmp = fts_children(pfts, 0);
    FTSENT * pchild;

    /*set list info*/
    fe_resetlist();
    g_totalBlockNum = 0;
    gl_totalSizeNum = 0;

    /*按照path中多个路径使用fts_link进行逐一定位*/
    for (pchild = ptmp; pchild != NULL; pchild = pchild->fts_link) {

        if (pchild->fts_info == FTS_ERR || pchild->fts_info == FTS_NS
            || pchild->fts_info == FTS_DNR) {
            fprintf(stderr,"ls: can't access %s: %s\n", pchild->fts_name, strerror(pchild->fts_errno));
            continue;
        }
        if(!parent) {
            //parent为null，不显示目录开关没开，跳过目录处理
            if (!IS_PARAM_DIR_NOT_DISPLAY(para_flag) && pchild->fts_info == FTS_D) {
                continue;
            }
        }
        else{
            //parent不为空表示处理目录，跳过.目录
            if ( pchild->fts_name[0] == '.' &&
            !IS_PARAM_INCLUDE_ENTRIES(para_flag) &&
            !IS_PARAM_ENTRIES_WITHOUT_DOT(para_flag)&&
            !IS_PARAM_F_NOT_SORTED(para_flag)) {
                continue;
            }
        }
        childrenPreProcess(pchild,para_flag);
    }
    childrenProcess(parent, para_flag);
    return 0;
}

void process_dir( FTS *pfts, unsigned int para_flag ){
    FTSENT * pnode;
    FTSENT * pchild;

    if(IS_PARAM_DIR_NOT_DISPLAY(para_flag)){
        /*print current name*/
        return;
    }
    //The fts_read() function returns a pointer to an FTSENT structure describing a file in the hierarchy.
    // Directories (that are readable and do not cause cycles) are visited at least twice, once in preorder and once in postorder.
    // All other files are visited at least once.
    // (Hard links between directories that do not cause cycles or symbolic links to symbolic links may cause files to be visited more than once, or directories more than twice.)
    //重新从fts文件结构树中读取单个文件或者目录的FTSENT
    while((pnode = fts_read(pfts)) != NULL)
    {
        //只处理目录文件
        switch (pnode->fts_info) {
            case FTS_D:
            {
                //The fts_children() function returns a pointer to an FTSENT structure describing
                // the first entry in a NULL-terminated linked list of the files in the directory represented by the
                // FTSENT structure most recently returned by fts_read()
                //调用fts_children获取当前pnode的所有children的link list
                //对于目录文件第一遍fts_children只有自己，第二次遍历会加上自己的所有孩子。如果不开recursive，只会打印自己。
                pchild = fts_children(pfts, 0);
                if (pchild == NULL&& errno !=0)
                    break;
                //处理当前pnode的文件目录，第一遍其linklist只有一个元素。第二遍会打印孩子中的元素，因孩子节点只会遍历一遍，所以不用担心重复。
                if(0 != file_ls(pfts, pnode, para_flag))
                    continue;
                if (!IS_PARAM_RECURSIVE(para_flag)){
                    //设置no descendants of pnode are visited
                    if(pchild != NULL && 0 != fts_set(pfts, pnode, FTS_SKIP)){
                        print_err("ls: fts_set err:errno %s\n",strerror(errno));
                    }
                }
                break;
            }
            case FTS_ERR:
            case FTS_DNR:
                fprintf(stderr,"ls: can't access %s: %s\n", pnode->fts_path, strerror(pnode->fts_errno));
                break;
            case FTS_DC:
                fprintf(stderr,"ls: %s causes a cycle in the tree \n", pnode->fts_path);
                break;
            default:
                break;
        }
    }
    return;
}
void traverse_path(unsigned int para_flag, char** path){

    int ftsoption = FTS_PHYSICAL;
    FTS* pfts = NULL;

    if(IS_PARAM_INCLUDE_ENTRIES(para_flag) || IS_PARAM_F_NOT_SORTED(para_flag))
        ftsoption = FTS_PHYSICAL | FTS_SEEDOT;

    if(NULL == (pfts = fts_open(path, ftsoption , (IS_PARAM_F_NOT_SORTED(para_flag)?0:comp)))){
        print_err("ls:fts open fail,errno:%d\n",errno);
    }
    /*go through all the files in path*/
    //处理输入列表中的所有路径，这些路径处理没有parent， 此处只处理文件
    file_ls(pfts,NULL,para_flag);
    /* go through all the directories in path*/
    //处理所有目录
    process_dir(pfts,para_flag);

    fts_close(pfts);
    if(errno != 0){
        print_err("ls:fts_close fail%d \n",errno);
    }
}

void init(unsigned int para_flag){

    fe_Initlist();

    if (getenv("BLOCKSIZE") != NULL){
        if ((g_blocksize = atoi(getenv("BLOCKSIZE"))) <= 0)
            g_blocksize = DEFAULT_BLOCKSIZE;
    }
    else
        g_blocksize = DEFAULT_BLOCKSIZE;

    g_totalBlockNum = 0;
    gl_totalSizeNum = 0;

    /*init compare method*/
    if (IS_PARAM_SORTBY_FILESIZE(para_flag)) {
        comp = IS_PARAM_REVERSE_SORTED(para_flag)? sortByRSize: sortBySize;
    } else if (IS_PARAM_SORTBY_MODTIME(para_flag)) {
        comp = IS_PARAM_REVERSE_SORTED(para_flag)? sortByRModTime: sortByModTime;
    } else if(IS_PARAM_FILE_ACCESSTIME(para_flag)){
        comp = IS_PARAM_REVERSE_SORTED(para_flag)? sortByRAccTime: sortByAccTime;
    } else if(IS_PARAM_FILE_CHANGETIME(para_flag)){
        comp = IS_PARAM_REVERSE_SORTED(para_flag)? sortByRChTime: sortByChTime;
    }
    else {
        comp = IS_PARAM_REVERSE_SORTED(para_flag) ? sortByRName : sortByName;
    }

    tzset();
}
int main(int argc, char* argv[]){
    int opt;
    const char *optstring = "AacdFfhiklnqRrSstuw";
    unsigned int para_flag = PARAM_NONE;
    char** path;
    int i = 0;

    /*special cases for the para flag*/
    if (getuid() == 0) {
        para_flag |= PARAM_A;
    }
    if(isatty(STDOUT_FILENO)){
        para_flag |= PARAM_Q;
    }else{
        para_flag |= PARAM_W;
    }

    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
            case 'A':
                para_flag |= PARAM_A_;
                break;
            case 'a':
                para_flag |= PARAM_A;
                break;
            case 'c':
                para_flag &= (~PARAM_U);
                para_flag |= PARAM_C;
                break;
            case 'd':
                para_flag |= PARAM_D;
                break;
            case 'F':
                para_flag |= PARAM_F_;
                break;
            case 'f':
                para_flag |= PARAM_F;
                break;
            case 'h':
                para_flag &= (~PARAM_K);
                para_flag |= PARAM_H;
                break;
            case 'i':
                para_flag |= PARAM_I;
                break;
            case 'k':
                para_flag &= (~PARAM_H);
                para_flag |= PARAM_K;
                break;
            case 'l':
                para_flag &= (~PARAM_N);
                para_flag |= PARAM_L;
                break;
            case 'n':
                para_flag &= (~PARAM_L);
                para_flag |= PARAM_N;
                break;
            case 'q':
                para_flag &= (~PARAM_W);
                para_flag |= PARAM_Q;
                break;
            case 'R':
                para_flag |= PARAM_R_;
                break;
            case 'r':
                para_flag |= PARAM_R;
                break;
            case 'S':
                para_flag |= PARAM_S_;
                break;
            case 's':
                para_flag |= PARAM_S;
                break;
            case 't':
                para_flag |= PARAM_T;
                break;
            case 'u':
                para_flag &= (~PARAM_C);
                para_flag |= PARAM_U;
                break;
            case 'w':
                para_flag &= (~PARAM_Q);
                para_flag |= PARAM_W;
                break;
            case '?':
                print_err("Usage: ls [−AacdFfhiklnqRrSstuw] [file ...]\n");
        }
    }
    if(optind == argc){
        path = (char**) malloc(2* sizeof(char*));
        if (NULL == path){
            print_err("ls:unable to malloc");
        }
        if(NULL == (path[0] = strdup("."))){
            free(path);
            print_err("ls:unable to strup");
        }
        path[1] = NULL;
        i = 2;
    }

    if (optind < argc) {
        path = (char**)malloc((argc-optind+1)*sizeof(char*));
        if (NULL == path){
            print_err("ls:unable to malloc");
        }
        for (i = 0; optind < argc; optind++,i++) {
            if (NULL == ( path[i] = strdup(argv[optind]))){
                while(--i) free(path[i]);
                free(path);
                print_err("ls: strdup err:%d\n",errno);
            }
        }
        path[i] = NULL;
    }

    init(para_flag);
    traverse_path(para_flag,path);

    /*free everything*/
    while(--i) free(path[i]);
    free(path);
    fe_freelist();
    return 0;
}
