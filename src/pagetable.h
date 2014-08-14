#ifndef _PAGETABLE_H
#define _PAGETABLE_H

#include "libgallocy.h"


void init_sqlite_memory();


class PageTable {

  public:
    PageTable() : database_path("pt.db") {

      init_sqlite_memory();
      open_database();
      create_tables();

      insert_page_table_entry(NULL);
      insert_page_table_entry(NULL);
      insert_page_table_entry(NULL);
      insert_page_table_entry(NULL);
      insert_page_table_entry(NULL);
      insert_page_table_entry(NULL);

    }

    void open_database();
    void create_tables();
    void insert_page_table_entry(void* ptr);

    static int noop_callback(void *not_used, int argc, char **argv, char **az_col_name);
    static int print_callback(void *not_used, int argc, char **argv, char **az_col_name);

  private:

    sqlite3 *db;
    std::string database_path;

};


extern PageTable pt;


#endif
