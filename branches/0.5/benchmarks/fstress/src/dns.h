/*
 * Copyright (c) 2001 Duke University -- Darrell Anderson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Duke University
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */ 

/*
 * [dns_name2addr]
 *
 * use dns to translate an ascii hostname into an ip address.
 *
 * PARAMETERS:
 * -> hostname       hostname, null terminated ascii string.
 * <- addr           ip address, in network byte order.
 *
 * RETURNS:
 *    int            0 for success, -1 for error.
 */
int dns_name2addr(char *hostname, struct in_addr *addr);

/*
 * [dns_addr2name]
 *
 * use dns to translate an ip address into an ascii hostname.
 *
 * PARAMETERS:
 * -> addr           ip address, in network byte order.
 * <- hostname       hostname, null terminated ascii string.
 * -> maxlen         size of the hostname buffer, in bytes.
 *
 * RETURNS:
 *    int            0 for success, -1 for error.
 */
int dns_addr2name(struct in_addr addr, char *hostname, int maxlen);
