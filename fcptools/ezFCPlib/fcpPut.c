/*
  This code is part of FCPTools - an FCP-based client library for Freenet

	Developers:
 	- David McNab <david@rebirthing.co.nz>
	- Jay Oliveri <ilnero@gmx.net>
	
  CopyLeft (c) 2001 by David McNab

	Currently maintained by Jay Oliveri <ilnero@gmx.net>
	
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "ezFCPlib.h"


extern int   crSockConnect(hFCP *hfcp);
extern void  crSockDisconnect(hFCP *hfcp);
extern char *crTmpFilename(void);

/* exported functions for fcptools codebase */

int put_file(hFCP *hfcp, char *key_filename, char *meta_filename);
int put_fec_splitfile(hFCP *hfcp, char *key_filename, char *meta_filename);


/* Private functions for internal use */
static int fec_segment_file(hFCP *hfcp, char *key_filename, char *meta_filename);
static int fec_encode_segment(hFCP *hfcp, char *key_filename, int segment);
static int fec_insert_blocks(hFCP *hfcp);


int put_file(hFCP *hfcp, char *key_filename, char *meta_filename)
{
	char buf[L_FILE_BLOCKSIZE+1];
	int kfd;
	int mfd;

	int rc;
	
	FILE *kfile;
	FILE *mfile;
	
  /* connect to Freenet FCP */
  if (crSockConnect(hfcp) != 0)
    return -1;
	
  /* create a put message */

  if (meta_filename) {
	
    snprintf(buf, L_FILE_BLOCKSIZE,
						 "ClientPut\nURI=%s\nHopsToLive=%x\nDataLength=%x\nMetadataLength=%x\nData\n",
						 hfcp->key->uri->uri_str,
						 hfcp->htl,
						 hfcp->key->size,
						 hfcp->key->metadata->size
						 );
	}
	else {
    snprintf(buf, L_FILE_BLOCKSIZE,
						 "ClientPut\nURI=%s\nHopsToLive=%x\nDataLength=%x\nData\n",
						 hfcp->key->uri->uri_str,
						 hfcp->htl,
						 hfcp->key->size
						 );
  }

	/* Send fcpID */
	if (send(hfcp->socket, _fcpID, 4, 0) == -1)
		 return -1;
		 
	/* Send ClientPut command */
	if (send(hfcp->socket, buf, strlen(buf), 0) == -1) {
		_fcpLog(FCP_LOG_DEBUG, "could not send ClientPut command");
		return -1;
	}

	/* Open key data file */
	if (!(kfile = fopen(key_filename, "rb"))) {
		_fcpLog(FCP_LOG_DEBUG, "could not open key file for reading");
		return -1;
	}

	/* Open metadata file */
	if (!(mfile = fopen(meta_filename, "rb"))) {
		_fcpLog(FCP_LOG_DEBUG, "could not open metadata file for reading");
		return -1;
	}

	kfd = fileno(kfile);
	mfd = fileno(mfile);

  /* send metadata */
	while ((rc = read(mfd, buf, L_FILE_BLOCKSIZE)) > 0) {

    if (send(hfcp->socket, buf, rc, 0) < 0) {
      _fcpLog(FCP_LOG_DEBUG, "could not write metadata to Freenet");
      return -1;
		}
	}
	fclose(mfile);

  /* send key data */
	while ((rc = read(kfd, buf, L_FILE_BLOCKSIZE)) > 0) {

    if (send(hfcp->socket, buf, rc, 0) < 0) {
      _fcpLog(FCP_LOG_DEBUG, "could not write key data to Freenet");
      return -1;
		}
	}
  fclose(kfile);

  /* expecting a success response */
  rc = _fcpRecvResponse(hfcp);
  
  switch (rc) {
  case FCPRESP_TYPE_SUCCESS:
    _fcpLog(FCP_LOG_DEBUG, "node returned Success message");
    break;

  case FCPRESP_TYPE_KEYCOLLISION:
    _fcpLog(FCP_LOG_DEBUG, "node returned KeyCollision message");
    break;

  case FCPRESP_TYPE_ROUTENOTFOUND:
    _fcpLog(FCP_LOG_DEBUG, "node returned RouteNotFound message");
    break;

  case FCPRESP_TYPE_FORMATERROR:
    _fcpLog(FCP_LOG_DEBUG, "node returned FormatError message");
    break;

  case FCPRESP_TYPE_FAILED:
    _fcpLog(FCP_LOG_DEBUG, "node returned Failed message");
    break;

  default:
    _fcpLog(FCP_LOG_DEBUG, "node returned unknown message id %d", rc);

    crSockDisconnect(hfcp);
    return -1;
  }
  
  /* finished with connection */
  crSockDisconnect(hfcp);
  
  if ((rc != FCPRESP_TYPE_SUCCESS) && (rc != FCPRESP_TYPE_KEYCOLLISION))
    return -1;

	/* Before returning, set all the hfcp-> fields proper, so that the response
		 struct can be re-used on next call to _fcpRecvResponse(). */
	
	hfcp->key->uri = _fcpCreateHURI();
	_fcpParseURI(hfcp->key->uri, hfcp->response.success.uri);

	return 0;
}


int put_fec_splitfile(hFCP *hfcp, char *key_filename, char *meta_filename)
{
	int index;
	int rc;

	rc = fec_segment_file(hfcp, key_filename, meta_filename);
	_fcpLog(FCP_LOG_DEBUG, "fec_segment_file returned %d", rc);

	for (index = 0; index < hfcp->key->segment_count; index++) {
		rc = fec_encode_segment(hfcp, key_filename, index);
		_fcpLog(FCP_LOG_DEBUG, "fec_encode_segment returned %d", index);
	}

	_fcpLog(FCP_LOG_DEBUG, "fec_encode_segment loop returned");

	return 0;
}


/**********************************************************************/

static int fec_segment_file(hFCP *hfcp, char *key_filename, char *meta_filename)
{
	/* TRIM */

	char buf[L_FILE_BLOCKSIZE+1];
	int rc;

	int index;
	int segment_count;

  /* connect to Freenet FCP */
  if (crSockConnect(hfcp) != 0) return -1;

	snprintf(buf, L_FILE_BLOCKSIZE,
					 "FECSegmentFile\nAlgoName=OnionFEC_a_1_2\nFileLength=%x\nEndMessage\n",
					 hfcp->key->size
					 );

	/* Send fcpID */
	if (send(hfcp->socket, _fcpID, 4, 0) == -1) return -1;
		 
	/* Send FECSegmentFile command */
	if (send(hfcp->socket, buf, strlen(buf), 0) == -1) {
		_fcpLog(FCP_LOG_DEBUG, "could not send FECSegmentFile command");
		return -1;
	}

	_fcpLog(FCP_LOG_DEBUG, "sent FECSegmentFile message");

	rc = _fcpRecvResponse(hfcp);
	if (rc != FCPRESP_TYPE_SEGMENTHEADER) {
		_fcpLog(FCP_LOG_DEBUG, "did not receive expected SegmentHeader response");
		return -1;
	}

	/* Allocate the area for all required segments */
	hfcp->key->segment_count = hfcp->response.segmentheader.segments;
	hfcp->key->segments = (hSegment **)malloc(sizeof (hSegment *) * hfcp->key->segment_count);

	/* Loop while there's more segments to receive */
	segment_count = hfcp->key->segment_count;
	index = 0;

	/* Loop through all segments and store information */
	while (index < segment_count) {
		hfcp->key->segments[index] = (hSegment *)malloc(sizeof (hSegment));

		/* get counts of data and check blocks */
		hfcp->key->segments[index]->db_count = hfcp->response.segmentheader.block_count;
		hfcp->key->segments[index]->cb_count = hfcp->response.segmentheader.checkblock_count;

		/* allocate space for data and check block handles */
		hfcp->key->segments[index]->data_blocks = (hBlock **)malloc(sizeof (hBlock *) * hfcp->key->segments[index]->db_count);
		hfcp->key->segments[index]->check_blocks = (hBlock **)malloc(sizeof (hBlock *) * hfcp->key->segments[index]->cb_count);

		snprintf(buf, L_FILE_BLOCKSIZE,
						 "SegmentHeader\nFECAlgorithm=%s\nFileLength=%x\nOffset=%x\n" \
						 "BlockCount=%x\nBlockSize=%x\nCheckBlockCount=%x\n" \
						 "CheckBlockSize=%x\nSegments=%x\nSegmentNum=%x\nBlocksRequired=%x\nEndMessage\n",

						 hfcp->response.segmentheader.fec_algorithm,
						 hfcp->response.segmentheader.filelength,
						 hfcp->response.segmentheader.offset,
						 hfcp->response.segmentheader.block_count,
						 hfcp->response.segmentheader.block_size,
						 hfcp->response.segmentheader.checkblock_count,
						 hfcp->response.segmentheader.checkblock_size,
						 hfcp->response.segmentheader.segments,
						 hfcp->response.segmentheader.segment_num,
						 hfcp->response.segmentheader.blocks_required
						 );

		hfcp->key->segments[index]->header_str = (char *)malloc(strlen(buf) + 1);
		strcpy(hfcp->key->segments[index]->header_str, buf);

		_fcpLog(FCP_LOG_DEBUG, "got segment %d/%d", index+1, segment_count);
	
		hfcp->key->segments[index]->filelength       = hfcp->response.segmentheader.filelength;
		hfcp->key->segments[index]->offset           = hfcp->response.segmentheader.offset;
		hfcp->key->segments[index]->block_count      = hfcp->response.segmentheader.block_count;
		hfcp->key->segments[index]->block_size       = hfcp->response.segmentheader.block_size;
		hfcp->key->segments[index]->checkblock_count = hfcp->response.segmentheader.checkblock_count;
		hfcp->key->segments[index]->checkblock_size  = hfcp->response.segmentheader.checkblock_size;
		hfcp->key->segments[index]->segments         = hfcp->response.segmentheader.segments;
		hfcp->key->segments[index]->segment_num      = hfcp->response.segmentheader.segment_num;
		hfcp->key->segments[index]->blocks_required  = hfcp->response.segmentheader.blocks_required;
		
		index++;

		/* Only if we're expecting more SegmentHeader messages
			 should we attempt to retrieve one ! */
		if (index < segment_count) rc = _fcpRecvResponse(hfcp);

	} /* End While - all segments now in hfcp container */

	/* Disconnect this connection.. its outlived it's purpose */
	crSockDisconnect(hfcp);

	return 0;
}


static int fec_encode_segment(hFCP *hfcp, char *key_filename, int segment)
{
	/* TRIM */

	char buf[L_FILE_BLOCKSIZE+1];
	int fd;
	int rc;

	int i, j; /* general purpose */
	int index;   /* segment index */
	int fi;   /* file index */
	int bi;   /* block index */

	int byte_count;
	int block_count;
	int segment_count;

	int data_len;
	int block_len;
	int metadata_len;
	int pad_len;

	hFCP *hfcp_tmp;
	FILE *file;


	data_len     = hfcp->key->segments[segment]->filelength;
	metadata_len = strlen(hfcp->key->segments[segment]->header_str);
	pad_len      = (hfcp->key->segments[segment]->block_count * hfcp->key->segments[segment]->block_size) - data_len;
	
	_fcpLog(FCP_LOG_DEBUG, "data_len=%d metadata_len=%d pad_len=%d total=%d",
					data_len, metadata_len, pad_len, data_len + metadata_len + pad_len);
	
	/* new connection to Freenet FCP */
	if (crSockConnect(hfcp) != 0) return -1;
	
	/* Send fcpID */
	if (send(hfcp->socket, _fcpID, 4, 0) == -1) return -1;
	
	snprintf(buf, L_FILE_BLOCKSIZE,
					 "FECEncodeSegment\nDataLength=%x\nData\n",
					 data_len + metadata_len + pad_len
					 );
	
	/* Send FECEncodeSegment message */
	if (send(hfcp->socket, buf, strlen(buf), 0) == -1) {
		_fcpLog(FCP_LOG_DEBUG, "could not write FECEncodeSegment message");
		return -1;
	}
	
	/* Send SegmentHeader */
	if (send(hfcp->socket, hfcp->key->segments[segment]->header_str, strlen(hfcp->key->segments[segment]->header_str), 0) == -1) {
		_fcpLog(FCP_LOG_DEBUG, "could not write initial SegmentHeader message");
		return -1;
	}
	
	/* Open file we are about to send */
	if (!(file = fopen(key_filename, "rb"))) {
		_fcpLog(FCP_LOG_DEBUG, "could not open file for reading");
		return -1;
	}
	fd = fileno(file);
	
	/* Write the data from the file, then write the pad blocks */
	/* data_len is the total length of the data file we're inserting */
	
	fi = data_len;
	while (fi) {
		int bytes;
		
		/* How many bytes are we writing this pass? */
		byte_count = (fi > L_FILE_BLOCKSIZE ? L_FILE_BLOCKSIZE: fi);
		
		/* read byte_count bytes from the file we're inserting */
		bytes = read(fd, buf, byte_count);
		
		_fcpLog(FCP_LOG_DEBUG, "read %d bytes; %d bytes total so far..", bytes, data_len - fi);
		
		if ((rc = send(hfcp->socket, buf, bytes, 0)) < 0) {
			_fcpLog(FCP_LOG_DEBUG, "could not write key data to socket: %s", strerror(errno));
			return -1;
		}
		
		_fcpLog(FCP_LOG_DEBUG, "wrote %d bytes to socket", bytes);
		
		/* decrement by number of bytes written to the socket */
		fi -= byte_count;
	}
	
	/* now write the pad bytes and end transmission.. */
	
	/* set the buffer to all zeroes so we can send 'em */
	memset(buf, 0, L_FILE_BLOCKSIZE);
	
	fi = pad_len;
	while (fi) {
		
		/* how many bytes are we writing this pass? */
		byte_count = (fi > L_FILE_BLOCKSIZE ? L_FILE_BLOCKSIZE: fi);
		
		if ((rc = send(hfcp->socket, buf, byte_count, 0)) < 0) {
			_fcpLog(FCP_LOG_DEBUG, "could not write zero-padded data to socket: %s", strerror(errno));
			return -1;
		}
		
		_fcpLog(FCP_LOG_DEBUG, "sent %d bytes..", byte_count);
		
		/* decrement i by number of bytes written to the socket */
		fi -= byte_count;
	}
	
	/* if the response isn't BlocksEncoded, we have a problem */
	if ((rc = _fcpRecvResponse(hfcp)) != FCPRESP_TYPE_BLOCKSENCODED) {
		_fcpLog(FCP_LOG_DEBUG, "did not receive expected BlocksEncoded message");
		return -1;
	}
	
	/* it is a BlocksEncoded message.. get the check blocks */
	block_len = hfcp->response.blocksencoded.block_size;
	
	for (bi=0; bi < hfcp->key->segments[segment]->cb_count; bi++) {
		
		/* We're expecting a DataChunk message */
		if ((rc = _fcpRecvResponse(hfcp)) != FCPRESP_TYPE_DATACHUNK) {
			_fcpLog(FCP_LOG_DEBUG, "did not receive expected DataChunk message");
			return -1;
		}
		
		hfcp->key->segments[segment]->check_blocks[bi] = _fcpCreateHBlock();
		hfcp->key->segments[segment]->check_blocks[bi]->filename = crTmpFilename();
		
		if (!(file = fopen(hfcp->key->segments[segment]->check_blocks[bi]->filename, "wb"))) {
			
			_fcpLog(FCP_LOG_DEBUG, "could not open file \"%s\" for writing check block %d",
							hfcp->key->segments[segment]->check_blocks[bi]->filename, bi);
			return -1;
		}
		fd = fileno(file);
		
		for (fi=0; fi < block_len; ) {
			byte_count = write(fd, hfcp->response.datachunk.data, hfcp->response.datachunk.length);
			
			if (byte_count != hfcp->response.datachunk.length) {
				_fcpLog(FCP_LOG_DEBUG, "error writing check block %d", bi);
				return -1;
			}
			
			fi += byte_count;
			
			/* only get the next DataChunk message if we're expecting one */
			if (fi < block_len)
				if ((rc = _fcpRecvResponse(hfcp)) != FCPRESP_TYPE_DATACHUNK) {
					_fcpLog(FCP_LOG_DEBUG, "did not receive expected DataChunk message (2)");
					return -1;
				}
		}
		
		if (fi != block_len) {
			_fcpLog(FCP_LOG_DEBUG, "bytes received for check block did not match with expected length");
			return -1;
		}
		
		/* Close the check block file */
		fclose(file);
	}

	return 0;
}


static int fec_insert_blocks(hFCP *hfcp)
{
#if 0
	hfcp_tmp = _fcpCreateHFCP();

	/* Loop through all the segments */
	for (index=0; index < segment_count; index++) {

		/* Loop through all the check blocks */
		for (bi=0; bi < hfcp->key->segments[segment]->cb_count; bi++) {
			
			rc = put_file(hfcp_tmp, hfcp->key->segments[segment]->check_blocks[bi]->filename, 0);
			_fcpLog(FCP_LOG_DEBUG, "inserted check block in file \"%s\"\nrc=%d\nURI=%s",
							hfcp->key->segments[segment]->data_blocks[bi]->filename,
							rc,
							hfcp_tmp->key->uri->uri_str
							);
		}
	}
	
	_fcpDestroyHFCP(hfcp_tmp);
	
	/* Exit properly */
	
	crSockDisconnect(hfcp);
#endif

	return 0;
}

