#include "BucketTree.h"

// --------- bucket ------------

using std::vector;
using std::list;
using std::ifstream;
using std::ofstream;
using std::pair;

typedef vector<uint32_t>::iterator Iter_id;
typedef vector<bucket*>::iterator Iter_son;
namespace fs = boost::filesystem;
namespace io = boost::iostreams;

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace attrs = boost::log::attributes;


src::logger bucket::lg = src::logger();

void bucket::logger_init() {
    bucket::lg.add_attribute("Class", attrs::constant< string > ("BuckObj "));
}

bucket::bucket():hit(false), parent(NULL) {}

bucket::bucket(const bucket & bk) : b_rule(bk) {
    sonList = vector<bucket*>();
    related_rules = vector<uint32_t>();
    hit = false;
    parent = NULL;
}

bucket::bucket(const string & b_str, const rule_list * rL) : b_rule(b_str) {
    for (size_t idx = 0; idx != rL->list.size(); ++idx)
        if (b_rule::match_rule(rL->list[idx]))
            related_rules.push_back(idx);
    hit = false;
    parent = NULL;
}

pair<double, size_t> bucket::split(const vector<size_t> & dim , rule_list *rList) {
    if (!sonList.empty())
        cleanson();

    uint32_t new_masks[4];
    size_t total_son_no = 1;

    for (size_t i = 0; i < 4; ++i) { // new mask
        new_masks[i] = addrs[i].mask;

        for (size_t j = 0; j < dim[i]; ++j) {
            if (~(new_masks[i]) == 0)
                return std::make_pair(-1, 0);

            new_masks[i] = (new_masks[i] >> 1) + (1 << 31);
            total_son_no *= 2;
        }
    }

    sonList.reserve(total_son_no);

    size_t total_rule_no = 0;
    size_t largest_rule_no = 0;

    for (size_t i = 0; i < total_son_no; ++i) {
        bucket * son_ptr = new bucket(*this);
        son_ptr->parent = this;

        uint32_t id = i;
        for (size_t j = 0; j < 4; ++j) { // new pref
            son_ptr->addrs[j].mask = new_masks[j];
            size_t incre = (~(new_masks[j]) + 1);
            son_ptr->addrs[j].pref += (id % (1 << dim[j]))*incre;
            id = (id >> dim[j]);
        }

        for (Iter_id iter = related_rules.begin(); iter != related_rules.end(); ++iter) { // rela rule
            if (son_ptr->match_rule(rList->list[*iter]))
                son_ptr->related_rules.push_back(*iter);
        }

        total_rule_no += son_ptr->related_rules.size();
        largest_rule_no = std::max(largest_rule_no, son_ptr->related_rules.size());

        sonList.push_back(son_ptr);
    }
    return std::make_pair(double(total_rule_no)/total_son_no, largest_rule_no);
}

int bucket::reSplit(const vector<size_t> & dim , rule_list *rList, bool apply = false) {
    if (!sonList.empty())
        cleanson();

    uint32_t new_masks[4];
    size_t total_son_no = 1;

    for (size_t i = 0; i < 4; ++i) { // new mask
        new_masks[i] = addrs[i].mask;
        for (size_t j = 0; j < dim[i]; ++j) {
            if (~(new_masks[i]) == 0)
                return (0 - rList->list.size()); // invalid

            new_masks[i] = (new_masks[i] >> 1) + (1 << 31);
            total_son_no *= 2;
        }
    }

    // debug
    /*
    if (apply) {
        BOOST_LOG(lg) <<" ";
        BOOST_LOG(lg) <<" ";
        BOOST_LOG(lg) <<"split bucket : " << get_str();
        stringstream ss;
        for (auto iter = related_rules.begin(); iter != related_rules.end(); ++iter) {
            if (rList->list[*iter].hit)
                ss << *iter<<"("<<rList->occupancy[*iter]<<")h, ";
            else
                ss << *iter<<"("<<rList->occupancy[*iter]<<"), ";
        }
        BOOST_LOG(lg) <<"rela: "<<ss.str();
    }*/

    sonList.reserve(total_son_no);
    set<size_t> to_cache_rules;
    int gain = 0;

    for (size_t i = 0; i < total_son_no; ++i) {
        bool to_cache = false;
        bucket * son_ptr = new bucket(*this);
        son_ptr->parent = this;

        uint32_t id = i;
        for (size_t j = 0; j < 4; ++j) { // new pref
            son_ptr->addrs[j].mask = new_masks[j];
            size_t incre = (~(new_masks[j]) + 1);
            son_ptr->addrs[j].pref += (id % (1 << dim[j]))*incre;
            id = id >> dim[j];
        }

        for (Iter_id iter = related_rules.begin(); iter != related_rules.end(); ++iter) { // rela rule
            if (son_ptr->match_rule(rList->list[*iter])) {
                son_ptr->related_rules.push_back(*iter);

                if (rList->list[*iter].hit) {
                    to_cache = true;
                }
            }
        }

        if (to_cache) {
            --gain; // cache one more bucket
            for (auto iter = son_ptr->related_rules.begin(); iter != son_ptr->related_rules.end(); ++iter) {
                to_cache_rules.insert(*iter);
                if (apply)  // apply the occupancy to the bucket
                    ++rList->occupancy[*iter];
            }
        }
        sonList.push_back(son_ptr);
    }


    if (apply) { // remove the occupancy of old bucket
        for (auto iter = related_rules.begin(); iter != related_rules.end(); ++iter)
            --rList->occupancy[*iter];
    } else {
        ++gain; // cache no old buck
        for (auto iter = related_rules.begin(); iter != related_rules.end(); ++iter) {
            if ((to_cache_rules.find(*iter) == to_cache_rules.end()) &&  // not cached
                    rList->occupancy[*iter] == 1)			 // dominantly found in this bucket
                ++gain;
        }
    }

    // debug
    /*
    if (apply) {
        for (auto iter_s = sonList.begin(); iter_s != sonList.end(); ++iter_s) {
            BOOST_LOG(lg) <<"son : " << (*iter_s)->get_str();
            stringstream ss;
            for (auto iter = (*iter_s)->related_rules.begin(); iter != (*iter_s)->related_rules.end(); ++iter) {
                if (rList->list[*iter].hit)
                    ss << *iter << "("<<rList->occupancy[*iter]<<")h, ";
                else
                    ss << *iter << "("<<rList->occupancy[*iter]<<"), ";

            }
            BOOST_LOG(lg) <<"rela: "<<ss.str();
        }
    }*/

    return gain;
}

vector<size_t> bucket::unq_comp(rule_list * rList) {
    vector<size_t> result;
    size_t sum = 0;
    for (size_t i = 0; i < 2; ++i) {
        set<size_t> comp;
        for (auto iter = related_rules.begin(); iter != related_rules.end(); ++iter) {
            size_t pref = rList->list[*iter].hostpair[i].pref;
            size_t mask = rList->list[*iter].hostpair[i].mask;
            comp.insert(pref);
            comp.insert(pref+mask);
        }
        result.push_back(comp.size()-1);
        sum += comp.size() - 1;
    }

    for (size_t i = 0; i < 2; ++i) {
        set<size_t> comp;
        for (auto iter = related_rules.begin(); iter != related_rules.end(); ++iter) {
            comp.insert(rList->list[*iter].portpair[i].range[0]);
            comp.insert(rList->list[*iter].portpair[i].range[1]);
        }
        result.push_back(comp.size()-1);
        sum += comp.size() - 1;
    }
    double avg = sum/4;

    vector<size_t> outstand;
    for (size_t i = 0; i < 4; ++i) {
        if (result[i] >= avg)
            outstand.push_back(i);
    }

    return outstand;
}

string bucket::get_str() const {
    stringstream ss;
    ss << b_rule::get_str() << "\t" << related_rules.size();
    return ss.str();
}

void bucket::cleanson() {
    for (auto iter = sonList.begin(); iter != sonList.end(); ++iter)
        delete (*iter);
    sonList.clear();
}

// ---------- bucket_tree ------------
bucket_tree::bucket_tree() {
    root = NULL;
    thres_soft = 0;
}

bucket_tree::bucket_tree(rule_list & rL, uint32_t thr, double pa_perc) {
    thres_hard = thr;
    thres_soft = thr*2;
    rList = &rL;
    root = new bucket(); // full address space
    for (uint32_t i = 0; i < rL.list.size(); i++)
        root->related_rules.insert(root->related_rules.end(), i);

    gen_candi_split();
    splitNode_fix(root);

    pa_rule_no = 1500 * pa_perc;
}

bucket_tree::~bucket_tree() {
    delNode(root);
}

pair<bucket *, int> bucket_tree::search_bucket(const addr_5tup& packet, bucket * buck) const {
    if (!buck->sonList.empty()) {
        size_t idx = 0;
        for (int i = 3; i >= 0; --i) {
            if (buck->cutArr[i] != 0) {
                idx = (idx << buck->cutArr[i]);
                size_t offset = (packet.addrs[i] - buck->addrs[i].pref);
                offset = offset/((~(buck->addrs[i].mask) >> buck->cutArr[i]) + 1);
                idx += offset;
            }
        }
        assert (idx < buck->sonList.size());
        return search_bucket(packet, buck->sonList[idx]);
    } else {
        buck->hit = true;
        int rule_id = -1;

        for (auto iter = buck->related_rules.begin(); iter != buck->related_rules.end(); ++iter) {
            if (rList->list[*iter].packet_hit(packet)) {
                rList->list[*iter].hit = true;
                rule_id = *iter;
                break;
            }
        }
        return std::make_pair(buck, rule_id);
    }
}

bucket * bucket_tree::search_bucket_seri(const addr_5tup& packet, bucket * buck) const {
    if (buck->sonList.size() != 0) {
        for (auto iter = buck->sonList.begin(); iter != buck->sonList.end(); ++iter)
            if ((*iter)->packet_hit(packet))
                return search_bucket_seri(packet, *iter);
        return NULL;
    } else {
        return buck;
    }
}

void bucket_tree::check_static_hit(const b_rule & traf_block, bucket* buck, set<size_t> & cached_rules, size_t & buck_count) {
    if (buck->sonList.empty()) { // bucket
        bool this_buck_hit = false;
        // a bucket is hit only when at least one rule is hit
        for (auto iter = buck->related_rules.begin(); iter != buck->related_rules.end(); ++iter) {
            if (traf_block.match_rule(rList->list[*iter])) {
                this_buck_hit = true;
                break;
            }
        }

        if (this_buck_hit) { // this bucket is hit
            for (auto iter = buck->related_rules.begin(); iter != buck->related_rules.end(); ++iter) {
                cached_rules.insert(*iter);
                if (traf_block.match_rule(rList->list[*iter])) {
                    rList->list[*iter].hit = true;
                }
            }
            ++buck_count;
            buck->hit = true; // only matching at least one rule is considered a bucket hit
        }
    } else {
        for (auto iter = buck->sonList.begin(); iter != buck->sonList.end(); ++iter) {

            if ((*iter)->overlap(traf_block))
                check_static_hit(traf_block, *iter, cached_rules, buck_count);
        }
    }
}


void bucket_tree::gen_candi_split(size_t cut_no) {
    if (cut_no == 0) {
        vector<size_t> base(4,0);
        candi_split.push_back(base);
    } else {
        gen_candi_split(cut_no-1);
        vector< vector<size_t> > new_candi_split;
        if (cut_no > 1)
            new_candi_split = candi_split;

        for (auto iter = candi_split.begin(); iter != candi_split.end(); ++iter) {
            for (size_t i = 0; i < 4; ++i) {
                vector<size_t> base = *iter;
                ++base[i];
                new_candi_split.push_back(base);
            }
        }
        candi_split = new_candi_split;
    }
}

void bucket_tree::splitNode_fix(bucket * ptr) {
    double cost = ptr->related_rules.size();
    if (cost < thres_soft)
        return;

    pair<double, size_t> opt_cost = std::make_pair(ptr->related_rules.size(), ptr->related_rules.size());
    vector<size_t> opt_cut;

    for (auto iter = candi_split.begin(); iter != candi_split.end(); ++iter) {
        auto cost = ptr->split(*iter, rList);

        if (cost.first < 0)
            continue;

        if (cost.first < opt_cost.first || ((cost.first == opt_cost.first) && (cost.second < opt_cost.second))) {
            opt_cut = *iter;
            opt_cost = cost;
        }
    }

    if (opt_cut.empty()) {
        ptr->cleanson();
        return;
    } else {
        ptr->split(opt_cut, rList);
        for (size_t i = 0; i < 4; ++i)
            ptr->cutArr[i] = opt_cut[i];

        for (auto iter = ptr->sonList.begin(); iter != ptr->sonList.end(); ++iter)
            splitNode_fix(*iter);
    }
}

void bucket_tree::pre_alloc() {
    vector<uint32_t> rela_buck_count(rList->list.size(), 0);
    INOallocDet(root, rela_buck_count);

    for (uint32_t i = 0; i< pa_rule_no; i++) {
        uint32_t count_m = 0;
        uint32_t idx;
        for (uint32_t i = 0; i < rela_buck_count.size(); i++) {
            if(rela_buck_count[i] > count_m) {
                count_m = rela_buck_count[i];
                idx = i;
            }
        }
        rela_buck_count[idx] = 0;
        pa_rules.insert(idx);
    }

    INOpruning(root);
}

void bucket_tree::dyn_adjust() {
    merge_bucket(root);
    print_tree("../para_src/tree_merge.dat");
    repart_bucket();
    rList->clearHitFlag();
}


void bucket_tree::INOallocDet (bucket * bk, vector<uint32_t> & rela_buck_count) const {
    for (Iter_id iter = bk->related_rules.begin(); iter != bk->related_rules.end(); iter++) {
        rela_buck_count[*iter] += 1;
    }
    for (Iter_son iter_s = bk->sonList.begin(); iter_s != bk->sonList.end(); iter_s ++) {
        INOallocDet(*iter_s, rela_buck_count);
    }
    return;
}

void bucket_tree::INOpruning (bucket * bk) {
    for (Iter_id iter = bk->related_rules.begin(); iter != bk->related_rules.end(); ) {
        if (pa_rules.find(*iter) != pa_rules.end())
            bk->related_rules.erase(iter);
        else
            ++iter;
    }

    if (bk->related_rules.size() < thres_hard) { // if after pruning there's no need to split
        for (Iter_son iter_s = bk->sonList.begin(); iter_s != bk->sonList.end(); iter_s++) {
            delNode(*iter_s);
        }
        bk->sonList.clear();
        return;
    }

    for (Iter_son iter_s = bk->sonList.begin(); iter_s != bk->sonList.end(); iter_s ++) {
        INOpruning(*iter_s);
    }
    return;
}

void bucket_tree::delNode(bucket * ptr) {
    for (Iter_son iter = ptr->sonList.begin(); iter!= ptr->sonList.end(); iter++) {
        delNode(*iter);
    }
    delete ptr;
}

// dynamic related
void bucket_tree::merge_bucket(bucket * ptr) { // merge using back order search
    if (!ptr->sonList.empty()) {
        for (auto iter = ptr->sonList.begin(); iter!= ptr->sonList.end(); ++iter) {
            merge_bucket(*iter);
        }
    } else
        return;

    bool at_least_one_hit = false;

    for (auto iter = ptr->sonList.begin(); iter != ptr->sonList.end(); ++iter) {  // don't merge if all empty
        if ((*iter)->hit)
            at_least_one_hit = true;
        else {
            if (!(*iter)->related_rules.empty())
                return;
        }
    }

    if (!at_least_one_hit)
        return;

    for (auto iter = ptr->sonList.begin(); iter != ptr->sonList.end(); ++iter) // remove the sons.
        delete *iter;
    ptr->sonList.clear();
    ptr->hit = true;
}

void bucket_tree::regi_occupancy(bucket * ptr, deque <bucket *>  & hitBucks) {
    if (ptr->sonList.empty() && ptr->hit) {
        ptr->hit = false;  // clear the hit flag
        hitBucks.push_back(ptr);
        for (auto iter = ptr->related_rules.begin(); iter != ptr->related_rules.end(); ++iter) {
            ++rList->occupancy[*iter];
        }
    }
    for (auto iter = ptr->sonList.begin(); iter != ptr->sonList.end(); ++iter)
        regi_occupancy(*iter, hitBucks);
}

void bucket_tree::repart_bucket() {
    deque<bucket *> proc_line;
    regi_occupancy(root, proc_line);

    size_t suc_counter = 0;
    auto proc_iter = proc_line.begin();

    while (!proc_line.empty()) {
        while(true) {
            if (proc_iter == proc_line.end())
                proc_iter = proc_line.begin();

            bool found = false;
            for (auto rule_iter = (*proc_iter)->related_rules.begin();
                    rule_iter != (*proc_iter)->related_rules.end();
                    ++rule_iter) {
                if (rList->occupancy[*rule_iter] == 1) {
                    found = true;
                    break;
                }
            }

            if (found)
                break;
            else{
                ++proc_iter;
		++suc_counter;
	    }

	    if (suc_counter == proc_line.size())
		    return;
        }

        bucket* to_proc_bucket = *proc_iter;

        vector<size_t> opt_cut;
        int opt_gain = -1; // totally greedy: no gain don't partition

        for (auto iter = candi_split.begin(); iter != candi_split.end(); ++iter) {
            int gain = to_proc_bucket->reSplit(*iter, rList);
            if (gain > opt_gain) {
                opt_gain = gain;
                opt_cut = *iter;
            }
        }

        if (opt_cut.empty()) {
            to_proc_bucket->cleanson();
            ++proc_iter; // keep the bucket
        } else {
		BOOST_LOG(bTree_log) << "success";
            proc_iter = proc_line.erase(proc_iter); // delete the bucket
	    suc_counter = 0;
            to_proc_bucket->reSplit(opt_cut, rList, true);

            for (size_t i = 0; i < 4; ++i)
                to_proc_bucket->cutArr[i] = opt_cut[i];

            for (auto iter = to_proc_bucket->sonList.begin(); // push son for immediate processing
                    iter != to_proc_bucket->sonList.end();
                    ++iter) {
                bool son_hit = false;
                for(auto r_iter = (*iter)->related_rules.begin(); r_iter != (*iter)->related_rules.end(); ++r_iter) {
                    if (rList->list[*r_iter].hit) {
                        son_hit = true;
                        break;
                    }
                }

                if (son_hit) {
                    proc_line.insert(proc_iter, *iter);
                    --proc_iter;
                }
            }
        }
    }
}


void bucket_tree::print_bucket(ofstream & in, bucket * bk, bool detail) { // const
    if (bk->sonList.empty()) {
        in << bk->get_str() << endl;
        if (detail) {
            in << "re: ";
            for (Iter_id iter = bk->related_rules.begin(); iter != bk->related_rules.end(); iter++) {
                in << *iter << " ";
            }
            in <<endl;
        }

    } else {
        for (Iter_son iter = bk->sonList.begin(); iter != bk->sonList.end(); iter++)
            print_bucket(in, *iter, detail);
    }
    return;
}



/* TEST USE Functions
 *
 */

void bucket_tree::search_test(const string & tracefile_str) {
    io::filtering_istream in;
    in.push(io::gzip_decompressor());
    ifstream infile(tracefile_str);
    in.push(infile);

    string str;
    cout << "Start search testing ... "<< endl;
    size_t cold_packet = 0;
    size_t hot_packet = 0;
    while (getline(in, str)) {
        addr_5tup packet(str, false);
        auto result = search_bucket(packet, root);
        if (result.first->related_rules.size() < 10) {
            ++cold_packet;
        } else {
            ++hot_packet;
        }

        if (result.first != (search_bucket_seri(packet, root))) {
            BOOST_LOG(bTree_log) << "Within bucket error: packet: " << str;
            BOOST_LOG(bTree_log) << "search_buck   res : " << result.first->get_str();
            BOOST_LOG(bTree_log) << "search_buck_s res : " << result.first->get_str();
        }
        if (result.second != rList->linear_search(packet)) {
            if (pa_rules.find(rList->linear_search(packet)) == pa_rules.end()) { // not pre-allocated
                BOOST_LOG(bTree_log) << "Search rule error: packet:" << str;
                if (result.second > 0)
                    BOOST_LOG(bTree_log) << "search_buck res : " << rList->list[result.second].get_str();
                else
                    BOOST_LOG(bTree_log) << "search_buck res : " << "None";

                BOOST_LOG(bTree_log) << "linear_sear res : " << rList->list[rList->linear_search(packet)].get_str();
            }
        }
    }

    BOOST_LOG(bTree_log) << "hot packets: "<< hot_packet;
    BOOST_LOG(bTree_log) << "cold packets: "<< cold_packet;
    cout << "Search testing finished ... " << endl;
}

void bucket_tree::static_traf_test(const string & file_str) {
    ifstream file(file_str);
    size_t counter = 0;
    set<size_t> cached_rules;
    size_t buck_count = 0;

    debug = false;
    for (string str; getline(file, str); ++counter) {
        vector<string> temp;
        boost::split(temp, str, boost::is_any_of("\t"));
        size_t r_exp = boost::lexical_cast<size_t>(temp.back());
        if (r_exp > 40) {
            --counter;
            continue;
        }

        b_rule traf_blk(str);
        check_static_hit(traf_blk, root, cached_rules, buck_count);
        if (counter > 80)
            break;
    }
    cout << "Cached: " << cached_rules.size() << " rules, " << buck_count << " buckets " <<endl;

    dyn_adjust();
    print_tree("../para_src/tree_split.dat");

    buck_count = 0;
    rList->clearHitFlag();
    cached_rules.clear();

    counter = 0;
    file.seekg(std::ios::beg);
    for (string str; getline(file, str); ++counter) {
        vector<string> temp;
        boost::split(temp, str, boost::is_any_of("\t"));
        size_t r_exp = boost::lexical_cast<size_t>(temp.back());
        if (r_exp > 40) {
            --counter;
            continue;
        }

        b_rule traf_blk(str);
        check_static_hit(traf_blk, root, cached_rules, buck_count);
        if (counter > 80)
            break;
    }


    deque<bucket *> proc_line;
    regi_occupancy(root, proc_line);

    size_t unused_count = 0;
    stringstream ss;
    for (auto iter = cached_rules.begin(); iter != cached_rules.end(); ++iter) {
        if (!rList->list[*iter].hit) {
            ++unused_count;
            ss<<*iter << "("<< rList->occupancy[*iter]<<") ";
        }
    }
    BOOST_LOG(bTree_log)<< "Unused rules: "<<ss.str();

    cout << "Cached: " << cached_rules.size() << " rules (" << unused_count << ") " << buck_count << " buckets " <<endl;

}

void bucket_tree::print_tree(const string & filename, bool det) { // const
    ofstream out(filename);
    print_bucket(out, root, det);
    out.close();
}
