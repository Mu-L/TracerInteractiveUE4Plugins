/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef HTML_TABLE_H

#define HTML_TABLE_H

#include "PxSimpleTypes.h"

// only compile this source code under windows!

#if PX_WINDOWS_FAMILY
// A simple code snippet to create an HTML document with multiple tables in it.
//

namespace nvidia
{

enum HtmlSaveType
{
  HST_SIMPLE_HTML,       // just a very simple HTML document containing the tables.
  HST_CSV,               // Saves the Tables out as comma seperated value text
  HST_TEXT,              // Saves the tables out in human readable text format.
  HST_TEXT_EXTENDED,     // Saves the tables out in human readable text format, but uses the MS-DOS style extended ASCII character set for the borders.
  HST_CPP,               // Save the document out as C++ code that can re-create it.  This is used for debugging the system.
  HST_XML,               // Save the document into an easily digestable XML format.
};

class HtmlDocument;
class HtmlTableInterface;

class HtmlTable
{
public:
  virtual void                setColumnColor(unsigned int column,unsigned int color) = 0; // set a color for a specific column.
  virtual void                setHeaderColor(unsigned int color) = 0;              // color for header lines
  virtual void                setFooterColor(unsigned int color) = 0;             // color for footer lines
  virtual void                setBodyColor(unsigned int color) = 0;
  virtual void                addHeader(const char *fmt,...) = 0;      // Add a column header, each column designtated by CSV.  If any single colum has a forward slash, it is treated as a multi-line header.
  virtual void                addColumn(const char *data) = 0 ;      // add this single string to the next column. Also an optional 'color' that will control the background color of this column, starting with the current row.
  virtual void                addColumn(float v) = 0 ;               // will add this floating point number, nicely formatted
  virtual void                addColumn(int v) = 0;                 // will add this integer number nicely formatted.
  virtual void                addColumn(unsigned int v) = 0;                 // will add this integer number nicely formatted.
  virtual void                addColumnHex(unsigned int v) = 0;                 // will add this as a hex string.
  virtual void                addCSV(bool newRow,const char *fmt,...) = 0;       // add this line of data as a set of columns, using the comma character as a seperator.
  virtual void                nextRow(void) = 0;                         // advance to the next row.
  virtual HtmlDocument       *getDocument(void) = 0;                     // return the parent document.
  virtual HtmlTableInterface *getHtmlTableInterface(void) = 0;
  virtual void                computeTotals(void) = 0; // compute and display totals of numeric columns when displaying this table.
  virtual void                excludeTotals(unsigned int column) = 0; // Column's are 1 base indexing.  Specifies a column to *excude* from totals, even if it contains numeric data.
  virtual void                addSort(const char *sort_name,unsigned int primary_key,bool primary_ascending,unsigned int secondary_key,bool secondary_ascending) = 0; // adds a sorted result.  You can set up mulitple sort requests for a single table.
  virtual unsigned int               getColor(unsigned int column,bool isHeader,bool isFooter)= 0; // returns color for this column, or header, or footer
  virtual void                setOrder(unsigned int order) = 0;
};

class HtmlDocument
{
public:
  virtual HtmlTable *    createHtmlTable(const char *heading) = 0;              // create a table and add it to the HTML document.
  virtual const char *   saveDocument(size_t &len,HtmlSaveType type) =0; // save the document to memory, return a pointer to that memory and the length of the file.
  virtual bool           saveExcel(const char *fname) = 0; // excel format can only be saved directly to files on disk, as it needs to create a sub-directory for the intermediate files.
  virtual void           releaseDocumentMemory(const char *mem) = 0;           // release memory previously allocated for a document save.
  virtual HtmlTableInterface *getHtmlTableInterface(void) = 0;
};

class HtmlTableInterface
{
public:
  virtual HtmlDocument * createHtmlDocument(const char *document_name) = 0;    // create an HTML document.
  virtual void           releaseHtmlDocument(HtmlDocument *document) = 0;      // release a previously created HTML document
}; // end of namespace


HtmlTableInterface *getHtmlTableInterface(void);
int                 getHtmlMemoryUsage(void);


};

#endif

#endif
