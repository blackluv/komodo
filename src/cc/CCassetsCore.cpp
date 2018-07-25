/******************************************************************************
 * Copyright © 2014-2018 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "CCassets.h"

/*
 The SetAssetFillamounts() and ValidateAssetRemainder() work in tandem to calculate the vouts for a fill and to validate the vouts, respectively.
 
 This pair of functions are critical to make sure the trading is correct and is the trickiest part of the assets contract.
 
 //vin.0: normal input
 //vin.1: unspendable.(vout.0 from buyoffer) buyTx.vout[0]
 //vin.2+: valid CC output satisfies buyoffer (*tx.vin[2])->nValue
 //vout.0: remaining amount of bid to unspendable
 //vout.1: vin.1 value to signer of vin.2
 //vout.2: vin.2 assetoshis to original pubkey
 //vout.3: CC output for assetoshis change (if any)
 //vout.4: normal output for change (if any)
 //vout.n-1: opreturn [EVAL_ASSETS] ['B'] [assetid] [remaining asset required] [origpubkey]
    ValidateAssetRemainder(remaining_price,tx.vout[0].nValue,nValue,tx.vout[1].nValue,tx.vout[2].nValue,totalunits);
 
 Yes, this is quite confusing...
 
 In ValudateAssetRemainder the naming convention is nValue is the coin/asset with the offer on the books and "units" is what it is being paid in. The high level check is to make sure we didnt lose any coins or assets, the harder to validate is the actual price paid as the "orderbook" is in terms of the combined nValue for the combined totalunits.
 
 We assume that the effective unit cost in the orderbook is valid and that that amount was paid and also that any remainder will be close enough in effective unit cost to not matter. At the edge cases, this will probably be not true and maybe some orders wont be practically fillable when reduced to fractional state. However, the original pubkey that created the offer can always reclaim it.
*/

bool ValidateAssetRemainder(int32_t sellflag,uint64_t remaining_price,uint64_t remaining_nValue,uint64_t orig_nValue,uint64_t received_nValue,uint64_t paidunits,uint64_t totalunits)
{
    uint64_t unitprice,recvunitprice,newunitprice=0;
    if ( orig_nValue == 0 || received_nValue == 0 || paidunits == 0 || totalunits == 0 )
    {
        fprintf(stderr,"ValidateAssetRemainder: orig_nValue == %llu || received_nValue == %llu || paidunits == %llu || totalunits == %llu\n",(long long)orig_nValue,(long long)received_nValue,(long long)paidunits,(long long)totalunits);
        return(false);
    }
    else if ( totalunits != (remaining_price + paidunits) )
    {
        fprintf(stderr,"ValidateAssetRemainder: totalunits %llu != %llu (remaining_price %llu + %llu paidunits)\n",(long long)totalunits,(long long)(remaining_price + paidunits),(long long)remaining_price,(long long)paidunits);
        return(false);
    }
    else if ( orig_nValue != (remaining_nValue + received_nValue) )
    {
        fprintf(stderr,"ValidateAssetRemainder: orig_nValue %llu != %llu (remaining_nValue %llu + %llu received_nValue)\n",(long long)orig_nValue,(long long)(remaining_nValue - received_nValue),(long long)remaining_nValue,(long long)received_nValue);
        return(false);
    }
    else
    {
        unitprice = (orig_nValue * COIN) / totalunits;
        recvunitprice = (received_nValue * COIN) / paidunits;
        if ( remaining_price != 0 )
            newunitprice = (remaining_nValue * COIN) / remaining_price;
        if ( recvunitprice < unitprice )
        {
            fprintf(stderr,"error recvunitprice %.16f < %.16f unitprice, new unitprice %.16f\n",(double)recvunitprice/(COIN*COIN),(double)unitprice/(COIN*COIN),(double)newunitprice/(COIN*COIN));
            return(false);
        }
        fprintf(stderr,"recvunitprice %.16f >= %.16f unitprice, new unitprice %.16f\n",(double)recvunitprice/(COIN*COIN),(double)unitprice/(COIN*COIN),(double)newunitprice/(COIN*COIN));
    }
    return(true);
}

bool SetAssetFillamounts(int32_t sellflag,uint64_t &received_nValue,uint64_t &remaining_price,uint64_t orig_nValue,uint64_t &paidunits,uint64_t totalunits)
{
    uint64_t remaining_nValue,unitprice; double dprice;
    
    if ( totalunits == 0 )
    {
        received_nValue = remaining_price = paidunits = 0;
        return(false);
    }
    if ( paidunits >= totalunits )
    {
        paidunits = totalunits;
        received_nValue = orig_nValue;
        remaining_price = 0;
        fprintf(stderr,"totally filled!\n");
        return(true);
    }
    remaining_price = (totalunits - paidunits);
    if ( sellflag == 0 )
    {
        unitprice = (orig_nValue * COIN) / totalunits;
        received_nValue = (paidunits * unitprice) / COIN;
    }
    else
    {
        dprice = (double)orig_nValue / totalunits;
        received_nValue = (paidunits * dprice);
        fprintf(stderr,"dprice %.8f orig %llu/total %llu, recv %llu\n",dprice,(long long)orig_nValue,(long long)totalunits,(long long)received_nValue);
    }
    if ( unitprice > 0 && received_nValue > 0 && received_nValue <= orig_nValue )
    {
        remaining_nValue = (orig_nValue - received_nValue);
        printf("total.%llu 0 paid.%llu, remaining %llu <- %llu (%llu - %llu)\n",(long long)totalunits,(long long)paidunits,(long long)remaining_nValue,(long long)(orig_nValue - received_nValue),(long long)orig_nValue,(long long)received_nValue);
        return(ValidateAssetRemainder(sellflag,remaining_price,remaining_nValue,orig_nValue,received_nValue,paidunits,totalunits));
    } else return(false);
}

CScript EncodeAssetCreateOpRet(uint8_t funcid,std::vector<uint8_t> origpubkey,std::string name,std::string description)
{
    CScript opret; uint8_t evalcode = EVAL_ASSETS;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << origpubkey << name << description);
    return(opret);
}

CScript EncodeAssetOpRet(uint8_t funcid,uint256 assetid,uint256 assetid2,uint64_t price,std::vector<uint8_t> origpubkey)
{
    CScript opret; uint8_t evalcode = EVAL_ASSETS;
    assetid = revuint256(assetid);
    switch ( funcid )
    {
        case 't':  case 'x': case 'o':
            opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << assetid);
            break;
        case 's': case 'b': case 'S': case 'B':
            opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << assetid << price << origpubkey);
            break;
        case 'E': case 'e':
            assetid2 = revuint256(assetid2);
            opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << assetid << assetid2 << price << origpubkey);
            break;
        default:
            fprintf(stderr,"EncodeOpRet: illegal funcid.%02x\n",funcid);
            opret << OP_RETURN;
            break;
    }
    return(opret);
}

uint8_t DecodeAssetOpRet(const CScript &scriptPubKey,uint256 &assetid,uint256 &assetid2,uint64_t &price,std::vector<uint8_t> &origpubkey)
{
    std::vector<uint8_t> vopret; uint8_t funcid=0,*script,e,f;
    GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    memset(&assetid,0,sizeof(assetid));
    memset(&assetid2,0,sizeof(assetid2));
    price = 0;
    if ( script[0] == EVAL_ASSETS )
    {
        funcid = script[1];
        //fprintf(stderr,"decode.[%c]\n",funcid);
        switch ( funcid )
        {
            case 'c': return(funcid);
                break;
            case 't':  case 'x': case 'o':
                if ( E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> assetid) != 0 )
                {
                    assetid = revuint256(assetid);
                    return(funcid);
                }
                break;
            case 's': case 'b': case 'S': case 'B':
                if ( E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> assetid; ss >> price; ss >> origpubkey) != 0 )
                {
                    assetid = revuint256(assetid);
                    //fprintf(stderr,"got price %llu\n",(long long)price);
                    return(funcid);
                }
                break;
            case 'E': case 'e':
                if ( E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> assetid; ss >> assetid2; ss >> price; ss >> origpubkey) != 0 )
                {
                    //fprintf(stderr,"got price %llu\n",(long long)price);
                    assetid = revuint256(assetid);
                    assetid2 = revuint256(assetid2);
                    return(funcid);
                }
                break;
            default:
                fprintf(stderr,"DecodeAssetOpRet: illegal funcid.%02x\n",funcid);
                funcid = 0;
                break;
        }
    }
    return(funcid);
}

bool SetAssetOrigpubkey(std::vector<uint8_t> &origpubkey,uint64_t &price,const CTransaction &tx)
{
    uint256 assetid,assetid2;
    if ( DecodeAssetOpRet(tx.vout[tx.vout.size()-1].scriptPubKey,assetid,assetid2,price,origpubkey) != 0 )
        return(true);
    else return(false);
}
           
bool GetAssetorigaddrs(struct CCcontract_info *cp,char *CCaddr,char *destaddr,const CTransaction& tx)
{
    uint256 assetid,assetid2; uint64_t price,nValue=0; int32_t n; uint8_t funcid; std::vector<uint8_t> origpubkey; CScript script;
    n = tx.vout.size();
    if ( n == 0 || (funcid= DecodeAssetOpRet(tx.vout[n-1].scriptPubKey,assetid,assetid2,price,origpubkey)) == 0 )
        return(false);
    if ( GetCCaddress(cp,CCaddr,pubkey2pk(origpubkey)) != 0 && Getscriptaddress(destaddr,CScript() << origpubkey << OP_CHECKSIG) != 0 )
        return(true);
    else return(false);
}

uint64_t IsAssetvout(uint64_t &price,std::vector<uint8_t> &origpubkey,const CTransaction& tx,int32_t v,uint256 refassetid)
{
    uint256 assetid,assetid2; uint64_t nValue=0; int32_t n; uint8_t funcid;
    if ( tx.vout[v].scriptPubKey.IsPayToCryptoCondition() != 0 ) // maybe check address too?
    {
        n = tx.vout.size();
        nValue = tx.vout[v].nValue;
        //fprintf(stderr,"CC vout v.%d of n.%d %.8f\n",v,n,(double)nValue/COIN);
        if ( v >= n-1 )
            return(0);
        if ( (funcid= DecodeAssetOpRet(tx.vout[n-1].scriptPubKey,assetid,assetid2,price,origpubkey)) == 0 )
        {
            fprintf(stderr,"null decodeopret\n");
            return(0);
        }
        else if ( funcid == 'c' )
        {
            if ( refassetid == tx.GetHash() && v == 0 )
                return(nValue);
        }
        else if ( (funcid == 'b' || funcid == 'B') && v == 0 ) // critical! 'b'/'B' vout0 is NOT asset
            return(0);
        else if ( funcid != 'E' )
        {
            if ( assetid == refassetid )
                return(nValue);
        }
        else if ( funcid == 'E' )
        {
            if ( v < 2 && assetid == refassetid )
                return(nValue);
            else if ( v == 2 && assetid2 == refassetid )
                return(nValue);
        }
    }
    //fprintf(stderr,"Isassetvout: normal output v.%d %.8f\n",v,(double)tx.vout[v].nValue/COIN);
    return(0);
}

uint64_t AssetValidateCCvin(struct CCcontract_info *cp,Eval* eval,char *CCaddr,char *origaddr,const CTransaction &tx,int32_t vini,CTransaction &vinTx)
{
    uint256 hashBlock; char destaddr[64];
    origaddr[0] = destaddr[0] = CCaddr[0] = 0;
    if ( tx.vin.size() < 2 )
        return eval->Invalid("not enough for CC vins");
    else if ( tx.vin[vini].prevout.n != 0 )
        return eval->Invalid("vin1 needs to be buyvin.vout[0]");
    else if ( eval->GetTxUnconfirmed(tx.vin[vini].prevout.hash,vinTx,hashBlock) == 0 )
    {
        int32_t z;
        for (z=31; z>=0; z--)
            fprintf(stderr,"%02x",((uint8_t *)&tx.vin[vini].prevout.hash)[z]);
        fprintf(stderr," vini.%d\n",vini);
        return eval->Invalid("always should find CCvin, but didnt");
    }
    else if ( Getscriptaddress(destaddr,vinTx.vout[tx.vin[vini].prevout.n].scriptPubKey) == 0 || strcmp(destaddr,(char *)cp->unspendableCCaddr) != 0 )
    {
        fprintf(stderr,"%s vs %s\n",destaddr,(char *)cp->unspendableCCaddr);
        return eval->Invalid("invalid vin AssetsCCaddr");
    }
    //else if ( vinTx.vout[0].nValue < 10000 )
    //    return eval->Invalid("invalid dust for buyvin");
    else if ( GetAssetorigaddrs(cp,CCaddr,origaddr,vinTx) == 0 )
        return eval->Invalid("couldnt get origaddr for buyvin");
    fprintf(stderr,"Got %.8f to origaddr.(%s)\n",(double)vinTx.vout[tx.vin[vini].prevout.n].nValue/COIN,origaddr);
    if ( vinTx.vout[0].nValue == 0 )
        return eval->Invalid("null value CCvin");
    return(vinTx.vout[0].nValue);
}

uint64_t AssetValidateBuyvin(struct CCcontract_info *cp,Eval* eval,uint64_t &tmpprice,std::vector<uint8_t> &tmporigpubkey,char *CCaddr,char *origaddr,const CTransaction &tx,uint256 refassetid)
{
    CTransaction vinTx; uint64_t nValue; uint256 assetid,assetid2; uint8_t funcid;
    CCaddr[0] = origaddr[0] = 0;
    if ( (nValue= AssetValidateCCvin(cp,eval,CCaddr,origaddr,tx,1,vinTx)) == 0 )
        return(0);
    else if ( vinTx.vout[0].scriptPubKey.IsPayToCryptoCondition() == 0 )
        return eval->Invalid("invalid normal vout0 for buyvin");
    else
    {
        //fprintf(stderr,"have %.8f checking assetid origaddr.(%s)\n",(double)nValue/COIN,origaddr);
        if ( (funcid= DecodeAssetOpRet(vinTx.vout[vinTx.vout.size()-1].scriptPubKey,assetid,assetid2,tmpprice,tmporigpubkey)) != 'b' && funcid != 'B' )
            return eval->Invalid("invalid opreturn for buyvin");
        else if ( refassetid != assetid )
            return eval->Invalid("invalid assetid for buyvin");
        //int32_t i; for (i=31; i>=0; i--)
        //    fprintf(stderr,"%02x",((uint8_t *)&assetid)[i]);
        //fprintf(stderr," AssetValidateBuyvin assetid for %s\n",origaddr);
    }
    return(nValue);
}

uint64_t AssetValidateSellvin(struct CCcontract_info *cp,Eval* eval,uint64_t &tmpprice,std::vector<uint8_t> &tmporigpubkey,char *CCaddr,char *origaddr,const CTransaction &tx,uint256 assetid)
{
    CTransaction vinTx; uint64_t nValue,assetoshis;
    fprintf(stderr,"AssetValidateSellvin\n");
    if ( (nValue= AssetValidateCCvin(cp,eval,CCaddr,origaddr,tx,1,vinTx)) == 0 )
        return(0);
    if ( (assetoshis= IsAssetvout(tmpprice,tmporigpubkey,vinTx,0,assetid)) == 0 )
        return eval->Invalid("invalid missing CC vout0 for sellvin");
    else return(assetoshis);
}

bool AssetExactAmounts(struct CCcontract_info *cp,uint64_t &inputs,int32_t starti,uint64_t &outputs,Eval* eval,const CTransaction &tx,uint256 assetid)
{
    CTransaction vinTx; uint256 hashBlock; int32_t i,numvins,numvouts; uint64_t assetoshis; std::vector<uint8_t> tmporigpubkey; uint64_t tmpprice;
    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    inputs = outputs = 0;
    for (i=starti; i<numvins; i++)
    {
        if ( (*cp->ismyvin)(tx.vin[i].scriptSig) != 0 )
        {
            if ( eval->GetTxUnconfirmed(tx.vin[i].prevout.hash,vinTx,hashBlock) == 0 )
            {
                fprintf(stderr,"i.%d starti.%d numvins.%d\n",i,starti,numvins);
                return eval->Invalid("always should find vin, but didnt");
            }
            else if ( (assetoshis= IsAssetvout(tmpprice,tmporigpubkey,vinTx,tx.vin[i].prevout.n,assetid)) != 0 )
            {
                fprintf(stderr,"vin%d %llu, ",i,(long long)assetoshis);
                inputs += assetoshis;
            }
        }
    }
    for (i=0; i<numvouts; i++)
    {
        if ( (assetoshis= IsAssetvout(tmpprice,tmporigpubkey,tx,i,assetid)) != 0 )
        {
            fprintf(stderr,"vout%d %llu, ",i,(long long)assetoshis);
            outputs += assetoshis;
        }
    }
    if ( inputs != outputs )
    {
        fprintf(stderr,"inputs %.8f vs %.8f outputs\n",(double)inputs/COIN,(double)outputs/COIN);
        return(false);
    }
    else return(true);
}
