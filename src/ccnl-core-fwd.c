 /*
 * @f ccnl-core-fwd.c
 * @b CCN lite, the collection of suite specific forwarding logics
 *
 * Copyright (C) 2011-14, Christian Tschudin, University of Basel
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * File history:
 * 2014-11-05 collected from the various fwd-XXX.c files
 */

typedef int (*matchfct2)(struct ccnl_pkt_s *p, struct ccnl_content_s *c);

// ----------------------------------------------------------------------
// content/data handling (common across all suites)
void
ccnl_fwd_handleContent(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                       struct ccnl_content_s *c)
{
    if (!c)
        return;
     // CONFORM: Step 2 (and 3)
#ifdef USE_NFN
    if (ccnl_nfnprefix_isNFN(c->name)) {
        if (ccnl_nfn_RX_result(relay, from, c))
            return;
        DEBUGMSG(VERBOSE, "no running computation found \n");
    }
#endif
    if (!ccnl_content_serve_pending(relay, c)) { // unsolicited content
        // CONFORM: "A node MUST NOT forward unsolicited data [...]"
        DEBUGMSG(DEBUG, "  removed because no matching interest\n");
        free_content(c);
        return;
    }
    if (relay->max_cache_entries != 0) { // it's set to -1 or a limit
        DEBUGMSG(DEBUG, "  adding content to cache\n");
        ccnl_content_add2cache(relay, c);
    } else {
        DEBUGMSG(DEBUG, "  content not added to cache\n");
        free_content(c);
    }
}

void
ccnl_fwd_handleContent2(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                        struct ccnl_pkt_s *pkt)
{
    struct ccnl_content_s *c;

    DEBUGMSG(DEBUG, "  data=<%s>\n", ccnl_prefix_to_path(pkt->pfx));

/*  mgmt messages for NDN?
#ifdef USE_SIGNATURES
        if (p->compcnt == 2 && !memcmp(p->comp[0], "ccnx", 4)
                && !memcmp(p->comp[1], "crypto", 6) &&
                from == relay->crypto_face) {
            rc = ccnl_crypto(relay, buf, p, from); goto Done;
        }
#endif // USE_SIGNATURES
*/

    // CONFORM: Step 1:
    for (c = relay->contents; c; c = c->next)
        if (buf_equal(c->pkt, pkt->buf))
            return; // content is dup

    c = ccnl_content_new(relay, pkt->suite,
                         &pkt->buf, &pkt->pfx, NULL /* ppkd */ ,
                         pkt->content, pkt->contlen);
    if (!c)
        return;

     // CONFORM: Step 2 (and 3)
#ifdef USE_NFN
    if (ccnl_nfnprefix_isNFN(c->name)) {
        if (ccnl_nfn_RX_result(relay, from, c))
            return;
        DEBUGMSG(VERBOSE, "no running computation found \n");
    }
#endif
    if (!ccnl_content_serve_pending(relay, c)) { // unsolicited content
        // CONFORM: "A node MUST NOT forward unsolicited data [...]"
        DEBUGMSG(DEBUG, "  removed because no matching interest\n");
        free_content(c);
        return;
    }
    if (relay->max_cache_entries != 0) { // it's set to -1 or a limit
        DEBUGMSG(DEBUG, "  adding content to cache\n");
        ccnl_content_add2cache(relay, c);
    } else {
        DEBUGMSG(DEBUG, "  content not added to cache\n");
        free_content(c);
    }
}

// ----------------------------------------------------------------------

struct pkt_ccnb_s {
    int scope, aok, minsfx, maxsfx;
    struct ccnl_buf_s *nonce, *ppkd;
};

struct pkt_ccntlv_s {
    struct ccnl_buf_s *keyid;
};

struct pkt_s {
    struct ccnl_buf_s *buf;        // the packet's bytes
    struct ccnl_prefix_s *pfx;     // prefix
    struct ccnl_prefix_s *tracing; // KITE
    unsigned char *content;        // raw content/payload bytes
    int contlen;                   // len
    union {
        struct pkt_ccnb_s   ccnb;
        struct pkt_ccntlv_s ccntlv;
    } p;                           // protocol specific fields
};

typedef int (*matchfct)(struct pkt_s *p, struct ccnl_content_s *c);

// ----------------------------------------------------------------------


// returns: 0=match, -1=otherwise
int
match_ccnb(struct pkt_s *p, struct ccnl_content_s *c)
{
    if (!ccnl_i_prefixof_c(p->pfx, p->p.ccnb.minsfx, p->p.ccnb.maxsfx, c))
        return -1;
    if (p->p.ccnb.ppkd && !buf_equal(p->p.ccnb.ppkd, c->details.ccnb.ppkd))
        return -1;
    // FIXME: should check stale bit in aok here
    return 0;
}

// returns: 0=match, -1=otherwise
int
match_ccntlv(struct pkt_s *p, struct ccnl_content_s *c)
{
    if (ccnl_prefix_cmp(c->name, NULL, p->pfx, CMP_EXACT))
        return -1;
    // TODO: check keyid
    // TODO: check freshness, kind-of-reply
    return 0;
}

// returns: 0=match, -1=otherwise
int
match_iottlv2(struct ccnl_pkt_s *p, struct ccnl_content_s *c)
{
    if (ccnl_prefix_cmp(c->name, NULL, p->pfx, CMP_EXACT))
        return -1;
    return 0;
}

int
match_ndntlv2(struct ccnl_pkt_s *p, struct ccnl_content_s *c)
{
    if (!ccnl_i_prefixof_c(p->pfx, p->s.ndntlv.minsuffix, p->s.ndntlv.maxsuffix, c))
        return -1;
    // FIXME: should check freshness (mbf) here
    // if (mbf) // honor "answer-from-existing-content-store" flag
    DEBUGMSG(DEBUG, "  matching content for interest, content %p\n",
                     (void *) c);
    return 0;
}

// ----------------------------------------------------------------------

int
ccnl_pkt_fwdOK(struct ccnl_pkt_s *pkt)
{
    switch (pkt->suite) {
#ifdef USE_SUITE_IOTTLV
    case CCNL_SUITE_IOTTLV:
        return pkt->s.iottlv.ttl > 0;
#endif
#ifdef USE_SUITE_NDNTLV
    case CCNL_SUITE_NDNTLV:
        return pkt->s.ndntlv.scope > 2;
#endif
    default:
        break;
    }

    return -1;
}

int
ccnl_interest_isSame(struct ccnl_interest_s *i, struct ccnl_pkt_s *pkt)
{
    switch (i->suite) {
#ifdef USE_SUITE_IOTTLV
    case CCNL_SUITE_IOTTLV:
        return i->suite == pkt->suite ||
                   !ccnl_prefix_cmp(i->prefix, NULL, pkt->pfx, CMP_EXACT);
#endif
#ifdef USE_SUITE_NDNTLV
    case CCNL_SUITE_NDNTLV:
        return i->details.ndntlv.minsuffix == pkt->s.ndntlv.minsuffix &&
               i->details.ndntlv.maxsuffix == pkt->s.ndntlv.maxsuffix &&
               ((!i->details.ndntlv.ppkl && !pkt->s.ndntlv.ppkl) ||
                    buf_equal(i->details.ndntlv.ppkl, pkt->s.ndntlv.ppkl));
#endif
    default:
        break;
    }
    return 1;
}


// ----------------------------------------------------------------------
// returns: -1=no match, 0=fullfilled
int
ccnl_fwd_answerFromCS(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                      struct pkt_s *p, matchfct cmp)
{
    struct ccnl_content_s *c;

    for (c = relay->contents; c; c = c->next) {
        if (c->suite != p->pfx->suite)
            continue;
        if (cmp(p, c))
            continue;

        DEBUGMSG(DEBUG, "  matching content for interest, content %p\n",
                 (void *) c);
        if (from->ifndx >= 0) {
            ccnl_nfn_monitor(relay, from, c->name, c->content,
                             c->contentlen);
            ccnl_face_enqueue(relay, from, buf_dup(c->pkt));
        } else {
            ccnl_app_RX(relay, c);
        }
        return 0;
    }
    return -1;
}

int
ccnl_fwd_answerFromCS2(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                       struct ccnl_pkt_s *p, matchfct2 cmp)
{
    struct ccnl_content_s *c;

    DEBUGMSG(DEBUG, "search in CS for %s %p\n", ccnl_prefix_to_path(p->pfx), (void*) relay->contents);

    for (c = relay->contents; c; c = c->next) {
      //        fprintf(stderr, "*\n");
        if (c->suite != p->pfx->suite)
            continue;
        if (cmp(p, c))
            continue;

        DEBUGMSG(DEBUG, "  matching content for interest, content %p\n",
                 (void *) c);
        if (from->ifndx >= 0) {
            ccnl_nfn_monitor(relay, from, c->name, c->content,
                             c->contentlen);
            ccnl_face_enqueue(relay, from, buf_dup(c->pkt));
        } else {
            ccnl_app_RX(relay, c);
        }
        return 0;
    }
    return -1;
}

int
ccnl_fwd_handleInterest(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                        struct ccnl_pkt_s *pkt, matchfct2 match)
{
    struct ccnl_interest_s *i;

    DEBUGMSG(DEBUG, "  interest=<%s>\n", ccnl_prefix_to_path(pkt->pfx));
    if (ccnl_nonce_isDup(relay, pkt)) {
        DEBUGMSG(DEBUG, "  dropped because of duplicate nonce\n");
        return 0;
    }
    /*
        filter here for link level management messages ...
        if (p->compcnt == 4 && !memcmp(p->comp[0], "ccnx", 4)) {
            rc = ccnl_mgmt(relay, buf, p, from); goto Done;
        }
    */
    if (!ccnl_fwd_answerFromCS2(relay, from, pkt, match))
        return 0;

    // CONFORM: Step 2: check whether interest is already known
#ifdef USE_KITE
    if (pkt.tracing) { // is a tracing interest
        for (i = relay->pit; i; i = i->next) {
        }
    }
#endif
    for (i = relay->pit; i; i = i->next)
        if (ccnl_interest_isSame(i, pkt))
            break;
        
    if (!i) { // this is a new/unknown I request: create and propagate
#ifdef USE_NFN
        if (ccnl_nfn_RX_request2(relay, from, pkt))
            return -1;
#endif
        if (!ccnl_pkt_fwdOK(pkt))
            return -1;
        i = ccnl_interest_new2(relay, from, pkt);
        DEBUGMSG(DEBUG,
            "  created new interest entry %p\n", (void *) i);
        ccnl_interest_propagate(relay, i);
    } else {
        if (ccnl_pkt_fwdOK(pkt) && (from->flags & CCNL_FACE_FLAGS_FWDALLI)) {
            DEBUGMSG(DEBUG, "  old interest, nevertheless propagated %p\n",
                     (void *) i);
            ccnl_interest_propagate(relay, i);
        }
    }
    if (i) { // store the I request, for the incoming face (Step 3)
        DEBUGMSG(DEBUG, "  appending interest entry %p\n", (void *) i);
        ccnl_interest_append_pending(i, from);
    }
    return 0;
}

// ----------------------------------------------------------------------

#ifdef USE_SUITE_CCNB

// helper proc: work on a message, top level type is already stripped
int
ccnl_ccnb_fwd(struct ccnl_relay_s *ccnl, struct ccnl_face_s *from,
              unsigned char **data, int *datalen)
{
    int rc= -1;
    struct ccnl_interest_s *i = 0;
    struct ccnl_content_s *c = 0;
    struct pkt_s pkt;

    DEBUGMSG(DEBUG, "ccnb fwd (%d bytes left)\n", *datalen);

    memset(&pkt, 0, sizeof(pkt));
    pkt.p.ccnb.scope = 3;
    pkt.p.ccnb.aok = 3;
    pkt.p.ccnb.maxsfx = CCNL_MAX_NAME_COMP;

    pkt.buf = ccnl_ccnb_extract(data, datalen, &pkt.p.ccnb.scope,
                                &pkt.p.ccnb.aok, &pkt.p.ccnb.minsfx,
                                &pkt.p.ccnb.maxsfx, &pkt.pfx,
                                &pkt.p.ccnb.nonce, &pkt.p.ccnb.ppkd,
                                &pkt.content, &pkt.contlen);
    if (!pkt.buf) {
        DEBUGMSG(DEBUG, "  parsing error or no prefix\n");
        goto Done;
    }
    if (pkt.p.ccnb.nonce && ccnl_nonce_find_or_append(ccnl, pkt.p.ccnb.nonce)) {
        DEBUGMSG(DEBUG, "  dropped because of duplicate nonce\n");
        goto Skip;
    }
    if (pkt.buf->data[0] == 0x01 && pkt.buf->data[1] == 0xd2) { // interest
        DEBUGMSG(DEBUG, "  interest=<%s>\n", ccnl_prefix_to_path(pkt.pfx));
        if (pkt.pfx->compcnt > 0 && pkt.pfx->comp[0][0] == (unsigned char) 0xc1)
            goto Skip;
        if (pkt.pfx->compcnt == 4 && !memcmp(pkt.pfx->comp[0], "ccnx", 4)) {
            rc = ccnl_mgmt(ccnl, pkt.buf, pkt.pfx, from);
            goto Done;
        }
        if (!ccnl_fwd_answerFromCS(ccnl, from, &pkt, match_ccnb))
            goto Skip;
        // CONFORM: Step 2: check whether interest is already known
        for (i = ccnl->pit; i; i = i->next) {
            if (i->suite == CCNL_SUITE_CCNB &&
                !ccnl_prefix_cmp(i->prefix, NULL, pkt.pfx, CMP_EXACT) &&
                i->details.ccnb.minsuffix == pkt.p.ccnb.minsfx &&
                i->details.ccnb.maxsuffix == pkt.p.ccnb.maxsfx && 
                ((!pkt.p.ccnb.ppkd && !i->details.ccnb.ppkd) ||
                   buf_equal(pkt.p.ccnb.ppkd, i->details.ccnb.ppkd)) )
                break;
        }
        // this is a new/unknown I request: create and propagate
#ifdef USE_NFN
        if (!i && ccnl_nfnprefix_isNFN(pkt.pfx)) { // NFN PLUGIN CALL
            if (ccnl_nfn_RX_request(ccnl, from, CCNL_SUITE_CCNB,
                                    &pkt.buf, &pkt.pfx,
                                    pkt.p.ccnb.minsfx, pkt.p.ccnb.maxsfx))
              // Since the interest msg may be required in future, it is not
              // possible to delete the interest/prefix here
              return rc;
        }
#endif
        if (!i) {
            i = ccnl_interest_new(ccnl, from, CCNL_SUITE_CCNB,
                                  &pkt.buf, &pkt.pfx,
                                  pkt.p.ccnb.minsfx, pkt.p.ccnb.maxsfx);
            if (pkt.p.ccnb.ppkd)
                i->details.ccnb.ppkd = pkt.p.ccnb.ppkd, pkt.p.ccnb.ppkd = NULL;
            if (i) { // CONFORM: Step 3 (and 4)
                DEBUGMSG(DEBUG, "  created new interest entry %p\n", (void *)i);
                if (pkt.p.ccnb.scope > 2)
                    ccnl_interest_propagate(ccnl, i);
            }
        } else if (pkt.p.ccnb.scope > 2 && (from->flags & CCNL_FACE_FLAGS_FWDALLI)) {
            DEBUGMSG(DEBUG, "  old interest, nevertheless propagated %p\n",
                     (void *) i);
            ccnl_interest_propagate(ccnl, i);
        }
        if (i) { // store the I request, for the incoming face (Step 3)
            DEBUGMSG(DEBUG, "  appending interest entry %p\n", (void *) i);
            ccnl_interest_append_pending(i, from);
        }
    } else { // content
        DEBUGMSG(DEBUG, "  content=<%s>\n", ccnl_prefix_to_path(pkt.pfx));
        
#ifdef USE_SIGNATURES
        if (pkt.pfx->compcnt == 2 && !memcmp(pkt.pfx->comp[0], "ccnx", 4)
                && !memcmp(pkt.pfx->comp[1], "crypto", 6) &&
                from == ccnl->crypto_face) {
            rc = ccnl_crypto(ccnl, pkt.buf, pkt.pfx, from);
            goto Done;
        }
#endif /*USE_SIGNATURES*/

        // CONFORM: Step 1:
        for (c = ccnl->contents; c; c = c->next)
            if (buf_equal(c->pkt, pkt.buf))
                goto Skip; // content is dup
        c = ccnl_content_new(ccnl, CCNL_SUITE_CCNB, &pkt.buf, &pkt.pfx,
                             &pkt.p.ccnb.ppkd, pkt.content, pkt.contlen);
        ccnl_fwd_handleContent(ccnl, from, c);
    }

Skip:
    rc = 0;
Done:
    free_prefix(pkt.pfx);
    free_3ptr_list(pkt.buf, pkt.p.ccnb.nonce, pkt.p.ccnb.ppkd);
    return rc;
}

// loops over a frame until empty or error
int
ccnl_ccnb_forwarder(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                    unsigned char **data, int *datalen)
{
    int rc = 0, num, typ;
    DEBUGMSG(DEBUG, "ccnl_RX_ccnb: %d bytes from face=%p (id=%d.%d)\n",
             *datalen, (void*)from, relay->id, from ? from->faceid : -1);

    while (rc >= 0 && *datalen > 0) {
        if (ccnl_ccnb_dehead(data, datalen, &num, &typ) || typ != CCN_TT_DTAG)
            return -1;
        switch (num) {
        case CCN_DTAG_INTEREST:
        case CCN_DTAG_CONTENTOBJ:
            rc = ccnl_ccnb_fwd(relay, from, data, datalen);
            continue;
#ifdef USE_FRAG
        case CCNL_DTAG_FRAGMENT2012:
            rc = ccnl_frag_RX_frag2012(ccnl_RX_ccnb, relay, from, data, datalen);
            continue;
        case CCNL_DTAG_FRAGMENT2013:
            rc = ccnl_frag_RX_CCNx2013(ccnl_RX_ccnb, relay, from, data, datalen);
            continue;
#endif
        default:
            DEBUGMSG(DEBUG, "  unknown datagram type %d\n", num);
            return -1;
        }
    }
    return rc;
}

#endif // USE_SUITE_CCNB

// ----------------------------------------------------------------------

#ifdef USE_SUITE_CCNTLV

// helper proc: work on a message, buffer is passed without the fixed header
int
ccnl_ccntlv_fwd(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                struct ccnx_tlvhdr_ccnx201412_s *hdrptr,
                unsigned char **data, int *datalen)
{
    int rc = -1;
    unsigned int lastchunknum;
    struct ccnl_interest_s *i = 0;
    struct ccnl_content_s *c = 0;
    unsigned char typ = hdrptr->pkttype;
    unsigned char hdrlen = *data - (unsigned char*)hdrptr;
    struct pkt_s pkt;

    DEBUGMSG(DEBUG, "ccnl_ccntlv_forwarder (%d bytes left, hdrlen=%d)\n",
             *datalen, hdrlen);
    memset(&pkt, 0, sizeof(pkt));

    pkt.buf = ccnl_ccntlv_extract(hdrlen, data, datalen, &pkt.pfx,
                                  &pkt.p.ccntlv.keyid, &lastchunknum,
                                  &pkt.content, &pkt.contlen);
    if (!pkt.buf) {
            DEBUGMSG(DEBUG, "  parsing error or no prefix\n");
            goto Done;
    }

    if (typ == CCNX_PT_Interest) {
        DEBUGMSG(DEBUG, "  interest=<%s>\n", ccnl_prefix_to_path(pkt.pfx));
        if (!ccnl_fwd_answerFromCS(relay, from, &pkt, match_ccntlv))
            goto Skip;
        DEBUGMSG(DEBUG, "  no matching content for interest\n");
        // CONFORM: Step 2: check whether interest is already known
        for (i = relay->pit; i; i = i->next) {
            if (i->suite != CCNL_SUITE_CCNTLV)
                continue;
            if (!ccnl_prefix_cmp(i->prefix, NULL, pkt.pfx, CMP_EXACT))
                // TODO check keyid
                break;
        }
        // this is a new/unknown I request: create and propagate
#ifdef USE_NFN
        if (!i && ccnl_nfnprefix_isNFN(pkt.pfx)) { // NFN PLUGIN CALL
            if (ccnl_nfn_RX_request(relay, from, CCNL_SUITE_CCNTLV,
                                    &pkt.buf, &pkt.pfx, 0, 0))
                goto Done;
        }
#endif
        if (!i) {
            i = ccnl_interest_new(relay, from, CCNL_SUITE_CCNTLV,
                               &pkt.buf, &pkt.pfx, 0, hdrptr->hoplimit - 1);
            if (i) { // CONFORM: Step 3 (and 4)
                // TODO keyID restriction
                DEBUGMSG(DEBUG, "  created new interest entry %p\n",
                         (void *) i);
                ccnl_interest_propagate(relay, i);
            }
        } else if ((from->flags & CCNL_FACE_FLAGS_FWDALLI)) {
            DEBUGMSG(DEBUG, "  old interest, nevertheless propagated %p\n",
                     (void *) i);
            ccnl_interest_propagate(relay, i);
        }
        if (i) { // store the I request, for the incoming face (Step 3)
            DEBUGMSG(DEBUG, "  appending interest entry %p\n", (void *) i);
            ccnl_interest_append_pending(i, from);
        }
    } else if (typ == CCNX_PT_NACK) {
        DEBUGMSG(DEBUG, "  NACK=<%s>\n", ccnl_prefix_to_path(pkt.pfx));
    } else if (typ == CCNX_PT_Data) { // data packet with content
        DEBUGMSG(DEBUG, "  data=<%s>\n", ccnl_prefix_to_path(pkt.pfx));

        // CONFORM: Step 1:
        for (c = relay->contents; c; c = c->next)
            if (buf_equal(c->pkt, pkt.buf))
                goto Skip; // content is dup
        c = ccnl_content_new(relay, CCNL_SUITE_CCNTLV, &pkt.buf, &pkt.pfx,
                             NULL, pkt.content, pkt.contlen);
        ccnl_fwd_handleContent(relay, from, c);
    }

Skip:
    rc = 0;
Done:
    free_prefix(pkt.pfx);
    ccnl_free(pkt.buf);

    return rc;
}

// process one CCNTLV packet, return <0 if no bytes consumed or error
int
ccnl_ccntlv_forwarder(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                      unsigned char **data, int *datalen)
{
    int payloadlen, hoplimit, endlen;
    unsigned short hdrlen;
    unsigned char *end;
    struct ccnx_tlvhdr_ccnx201412_s *hp;

    DEBUGMSG(DEBUG, "ccnl_RX_ccntlv: %d bytes from face=%p (id=%d.%d)\n",
             *datalen, (void*)from, relay->id, from ? from->faceid : -1);

    if (**data != CCNX_TLV_V0 ||
                        *datalen < sizeof(struct ccnx_tlvhdr_ccnx201412_s))
        return -1;

    hp = (struct ccnx_tlvhdr_ccnx201412_s*) *data;
    hdrlen = hp->hdrlen; // ntohs(hp->hdrlen);
    if (hdrlen > *datalen) // not enough bytes for a full header
        return -1;

    payloadlen = ntohs(hp->pktlen) - hdrlen;
    *data += hdrlen;
    *datalen -= hdrlen;
    if (payloadlen <= 0 ||
              payloadlen > *datalen) // not enough data to reconstruct message
            return -1;

    end = *data + payloadlen;
    endlen = *datalen - payloadlen;

    hoplimit = hp->hoplimit - 1;
    if ((hp->pkttype == CCNX_PT_Interest || hp->pkttype == CCNX_PT_NACK)
                                           && hoplimit <= 0) { // drop it
        *data = end;
        *datalen = endlen;
        return 0;
    } else
        hp->hoplimit = hoplimit;

    return ccnl_ccntlv_fwd(relay, from, hp, data, datalen);
}

#endif // USE_SUITE_CCNTLV

// ----------------------------------------------------------------------

#ifdef USE_SUITE_IOTTLV

// process one IOTTLV packet, return <0 if no bytes consumed or error
int
ccnl_iottlv_forwarder2(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                      unsigned char **data, int *datalen)
{
    int typ, len, rc = -1;
    unsigned char *start = *data;
    struct ccnl_pkt_s pkt;

    DEBUGMSG(TRACE, "ccnl_iottlv_forwarder2 (%d bytes left)\n", *datalen);

    memset(&pkt, 0, sizeof(pkt));

    if (ccnl_iottlv_dehead(data, datalen, &typ, &len))
        return -1;
    // typ must be Request or Reply

    if (ccnl_iottlv_bytes2pkt(start, data, datalen, &pkt)) {
        DEBUGMSG(WARNING, "  parsing error or no prefix\n");
        goto Done;
    }
    if (typ == IOT_TLV_Request) {
        if (ccnl_fwd_handleInterest(relay, from, &pkt, match_iottlv2))
            goto Done;
    } else // data packet with content -------------------------------------
        ccnl_fwd_handleContent2(relay, from, &pkt);
    rc = 0;
Done:
    free_prefix(pkt.pfx);
    ccnl_free(pkt.buf);
    return rc;
}

#endif // USE_SUITE_IOTTLV

// ----------------------------------------------------------------------

#ifdef USE_SUITE_NDNTLV

// process one NDNTLV packet, return <0 if no bytes consumed or error
int
ccnl_ndntlv_forwarder2(struct ccnl_relay_s *relay, struct ccnl_face_s *from,
                       unsigned char **data, int *datalen)
{
    int typ, len, rc = -1;
    unsigned char *start = *data;
    struct ccnl_pkt_s pkt;

    DEBUGMSG(DEBUG, "ccnl_ndntlv_forwarder2 (%d bytes left)\n", *datalen);

    memset(&pkt, 0, sizeof(pkt));
    pkt.s.ndntlv.scope = 3;
    pkt.s.ndntlv.maxsuffix = CCNL_MAX_NAME_COMP;

    if (ccnl_ndntlv_dehead(data, datalen, &typ, &len) || len > *datalen)
        return -1;
    if (ccnl_ndntlv_bytes2pkt(start, data, datalen, &pkt)) {
        DEBUGMSG(DEBUG, "  parsing error or no prefix\n");
        goto Done;
    }
    if (typ == NDN_TLV_Interest) {
        if (ccnl_fwd_handleInterest(relay, from, &pkt, match_ndntlv2))
            goto Done;
    } else  // data packet with content -------------------------------------
        ccnl_fwd_handleContent2(relay, from, &pkt);
    rc = 0;
Done:
    free_prefix(pkt.pfx);
    free_3ptr_list(pkt.buf, pkt.s.ndntlv.nonce, pkt.s.ndntlv.ppkl);

    return rc;
}

#endif // USE_SUITE_NDNTLV

// eof
