#ifndef _qe_h_
#define _qe_h_

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <climits>
#include <cfloat>

#include "../rbf/rbfm.h"
#include "../rm/rm.h"
#include "../ix/ix.h"

# define QE_EOF (-1)  // end of the index scan

using namespace std;

#define QE_FAIL_TO_SPLIT_TABLE_ATTRIBUTE 110
#define QE_FAIL_TO_FIND_CONDITION_ATTRIBUTE 111
#define QE_FAIL_TO_LOAD_INNER_DATA 112

// get the table and condition attribute name from table.attribute
RC getTableAttributeName(const string &tableAttribute,
		string &table, string &attribute);
// copy the data according to the type
void copyData(void *dest, const void *src, const AttrType &type);
// move the pointer according to the type
void movePointer(char *&data, const AttrType &type);
// compare values
bool compareValues(const char *lhs, const char *rhs,
		const CompOp &compOp, const AttrType &type);
template <typename T>
bool compareValueTemplate(T const &lhs, T const &rhs, const CompOp &compOp) {
	switch(compOp) {
	case EQ_OP:
		return lhs == rhs;
		break;
	case LT_OP:
		return lhs < rhs;
		break;
	case GT_OP:
		return lhs > rhs;
		break;
	case LE_OP:
		return lhs <= rhs;
		break;
	case GE_OP:
		return lhs >= rhs;
		break;
	case NE_OP:
		return lhs != rhs;
		break;
	default:
		return true;
	}
	return true;
}
// get a record size according to the attributes
int getRecordSize(char *data, const vector<Attribute> &attrs);

typedef enum{ MIN = 0, MAX, SUM, AVG, COUNT } AggregateOp;


// The following functions use  the following
// format for the passed data.
//    For int and real: use 4 bytes
//    For varchar: use 4 bytes for the length followed by
//                          the characters

struct Value {
    AttrType type;          // type of value
    void     *data;         // value
};


struct Condition {
    string lhsAttr;         // left-hand side attribute
    CompOp  op;             // comparison operator
    bool    bRhsIsAttr;     // TRUE if right-hand side is an attribute and not a value; FALSE, otherwise.
    string rhsAttr;         // right-hand side attribute if bRhsIsAttr = TRUE
    Value   rhsValue;       // right-hand side value if bRhsIsAttr = FALSE
};


class Iterator {
    // All the relational operators and access methods are iterators.
    public:
        virtual RC getNextTuple(void *data) = 0;
        virtual void getAttributes(vector<Attribute> &attrs) const = 0;
        virtual ~Iterator() {};
};

class BlockBuffer {
private:
	unsigned numPages;
	const unsigned size;					// size = numPages * PAGE_SIZE
	unsigned bufferUsage;				// current buffer size
	Iterator *iter;

	char *buffer;					// hold all data
	char *curBufferPointer;			// record the location of each tuple

	unsigned curIndex;
	vector<unsigned> dataPositions;	// record the pos of each tuple in buffer
	vector<unsigned> dataSizes;		// record the size of each tuple in the buffer

	char inputBuffer[PAGE_SIZE];

	vector<Attribute> attrs;
private:
	void loadNextBlock();
public:
	BlockBuffer(unsigned numPages, Iterator *iter);
	~BlockBuffer();
	RC getNextTuple(void *data);
};

class TableScan : public Iterator
{
    // A wrapper inheriting Iterator over RM_ScanIterator
    public:
        RelationManager &rm;
        RM_ScanIterator *iter;
        string tableName;
        vector<Attribute> attrs;
        vector<string> attrNames;
        RID rid;

        TableScan(RelationManager &rm, const string &tableName, const char *alias = NULL):rm(rm)
        {
        	//Set members
        	this->tableName = tableName;

            // Get Attributes from RM
            rm.getAttributes(tableName, attrs);

            // Get Attribute Names from RM
            unsigned i;
            for(i = 0; i < attrs.size(); ++i)
            {
                // convert to char *
                attrNames.push_back(attrs[i].name);
            }

            // Call rm scan to get iterator
            iter = new RM_ScanIterator();
            rm.scan(tableName, "", NO_OP, NULL, attrNames, *iter);

            // Set alias
            if(alias) this->tableName = alias;
        };

        // Start a new iterator given the new compOp and value
        void setIterator()
        {
            iter->close();
            delete iter;
            iter = new RM_ScanIterator();
            rm.scan(tableName, "", NO_OP, NULL, attrNames, *iter);
        };

        RC getNextTuple(void *data)
        {
            return iter->getNextTuple(rid, data);
        };

        void getAttributes(vector<Attribute> &attrs) const
        {
            attrs.clear();
            attrs = this->attrs;
            unsigned i;

            // For attribute in vector<Attribute>, name it as rel.attr
            for(i = 0; i < attrs.size(); ++i)
            { // jump the first, because it is the version number
                string tmp = tableName;
                tmp += ".";
                tmp += attrs[i].name;
                attrs[i].name = tmp;
            }
        };

        ~TableScan()
        {
        	iter->close();
        };
};


class IndexScan : public Iterator
{
    // A wrapper inheriting Iterator over IX_IndexScan
    public:
        RelationManager &rm;
        RM_IndexScanIterator *iter;
        string tableName;
        string attrName;
        vector<Attribute> attrs;
        char key[PAGE_SIZE];
        RID rid;

        IndexScan(RelationManager &rm, const string &tableName, const string &attrName, const char *alias = NULL):rm(rm)
        {
        	// Set members
        	this->tableName = tableName;
        	this->attrName = attrName;


            // Get Attributes from RM
            rm.getAttributes(tableName, attrs);

            // Call rm indexScan to get iterator
            iter = new RM_IndexScanIterator();
            rm.indexScan(tableName, attrName, NULL, NULL, true, true, *iter);

            // Set alias
            if(alias) this->tableName = alias;
        };

        // Start a new iterator given the new key range
        void setIterator(void* lowKey,
                         void* highKey,
                         bool lowKeyInclusive,
                         bool highKeyInclusive)
        {
            iter->close();
            delete iter;
            iter = new RM_IndexScanIterator();
            rm.indexScan(tableName, attrName, lowKey, highKey, lowKeyInclusive,
                           highKeyInclusive, *iter);
        };

        RC getNextTuple(void *data)
        {
            int rc = iter->getNextEntry(rid, key);
            if(rc == 0)
            {
                rc = rm.readTuple(tableName.c_str(), rid, data);
            }
            return rc;
        };

        void getAttributes(vector<Attribute> &attrs) const
        {
            attrs.clear();
            attrs = this->attrs;
            unsigned i;

            // For attribute in vector<Attribute>, name it as rel.attr
            for(i = 0; i < attrs.size(); ++i)
            {
                string tmp = tableName;
                tmp += ".";
                tmp += attrs[i].name;
                attrs[i].name = tmp;
            }
        };

        ~IndexScan()
        {
            iter->close();
        };
};


class Filter : public Iterator {
    // Filter operator
    public:
        Filter(Iterator *input,                         // Iterator of input R
               const Condition &condition               // Selection condition
        );
        ~Filter();

        RC getNextTuple(void *data);
        // For attribute in vector<Attribute>, name it as rel.attr
        void getAttributes(vector<Attribute> &attrs) const;
    private:
        Iterator *iter;
        string lhsAttr;
        CompOp compOp;
        AttrType type;
        vector<Attribute> attributeNames;

        char value[PAGE_SIZE];
        char tempData[PAGE_SIZE];
    	bool initStatus;

    	void copyValue(const Value &input);
    	bool compareValue(void *input);
};


class Project : public Iterator {
    // Projection operator
    public:
        Project(Iterator *input,                            // Iterator of input R
                const vector<string> &attrNames);           // vector containing attribute names
        ~Project();

        RC getNextTuple(void *data);
        // For attribute in vector<Attribute>, name it as rel.attr
        void getAttributes(vector<Attribute> &attrs) const;
    private:
        vector<Attribute> attrs;
        vector<Attribute> originAttrs;
        unordered_set<string> checkExistAttrNames;
        bool initStatus;
        Iterator *iter;
        char tempData[PAGE_SIZE];
};


class NLJoin : public Iterator {
    // Nested-Loop join operator
    public:
        NLJoin(Iterator *leftIn,                             // Iterator of input R
               TableScan *rightIn,                           // TableScan Iterator of input S
               const Condition &condition,                   // Join condition
               const unsigned numPages                       // Number of pages can be used to do join (decided by the optimizer)
        );
        ~NLJoin();

        RC getNextTuple(void *data);
        // For attribute in vector<Attribute>, name it as rel.attr
        void getAttributes(vector<Attribute> &attrs) const;
    private:
        BlockBuffer blockBuffer;
        BlockBuffer blockBufferRight;
        Iterator *leftIter;
        vector<Attribute> leftAttrs;
        TableScan *rightIter;
        vector<Attribute> rightAttrs;
        Condition condition;

        AttrType compAttrType;
        vector<Attribute> attrs;

        unsigned numPages;
        bool initStatus;

        bool needLoadNextLeftValue;
        char curLeftValue[PAGE_SIZE];
        char curLeftConditionValue[PAGE_SIZE];
        char curRightValue[PAGE_SIZE];
        char curRightConditionValue[PAGE_SIZE];

        RC getAttributeValue(char *data,
        		char *attrData, const vector<Attribute> &attrs,
        		const string &conditionAttr);
};


class INLJoin : public Iterator {
    // Index Nested-Loop join operator
    public:
        INLJoin(Iterator *leftIn,                               // Iterator of input R
                IndexScan *rightIn,                             // IndexScan Iterator of input S
                const Condition &condition,                     // Join condition
                const unsigned numPages                         // Number of pages can be used to do join (decided by the optimizer)
        );

        ~INLJoin();

        RC getNextTuple(void *data);
        // For attribute in vector<Attribute>, name it as rel.attr
        void getAttributes(vector<Attribute> &attrs) const;
    private:
        BlockBuffer blockBuffer;
        Iterator *leftIter;
        vector<Attribute> leftAttrs;
        IndexScan *rightIter;
        vector<Attribute> rightAttrs;
        Condition condition;

        AttrType compAttrType;
        vector<Attribute> attrs;

        unsigned numPages;
        bool initStatus;

        bool needLoadNextLeftValue;
        char curLeftValue[PAGE_SIZE];
        char curLeftConditionValue[PAGE_SIZE];
        char curRightValue[PAGE_SIZE];
        char curRightConditionValue[PAGE_SIZE];

        RC getAttributeValue(char *data,
        		char *attrData, const vector<Attribute> &attrs,
        		const string &conditionAttr);
        void setRightIterator(char *leftValue);
};

#define AGG_SINGLE_MODE 0
#define AGG_GROUP_MODE 1
// typedef enum{ MIN = 0, MAX, SUM, AVG, COUNT } AggregateOp;
class Aggregate : public Iterator {
    // Aggregation operator
    public:
        Aggregate(Iterator *input,                              // Iterator of input R
                  Attribute aggAttr,                            // The attribute over which we are computing an aggregate
                  AggregateOp op                                // Aggregate operation
        );

        // Extra Credit
        Aggregate(Iterator *input,                              // Iterator of input R
                  Attribute aggAttr,                            // The attribute over which we are computing an aggregate
                  Attribute gAttr,                              // The attribute over which we are grouping the tuples
                  AggregateOp op                                // Aggregate operation
        );

        ~Aggregate(){};

        RC getNextTuple(void *data);
        // Please name the output attribute as aggregateOp(aggAttr)
        // E.g. Relation=rel, attribute=attr, aggregateOp=MAX
        // output attrname = "MAX(rel.attr)"
        void getAttributes(vector<Attribute> &attrs) const;
    private:
        bool initStatus;
        Iterator *iter;
        vector<Attribute> attrs;
        Attribute aggAttr;
        Attribute gAttr;
        RC aggMode;
        AggregateOp op;
        char readValue[PAGE_SIZE];
        void getNextTuple_single(void *data);
        void singleMax(void *data);
        void singleMin(void *data);
        void singleSum(void *data);
        void singleAvg(void *data);
        void singleCount(void *data);

        char str[PAGE_SIZE];
    	unordered_map<int, int> group_int_int;
    	unordered_map<float, int> group_float_int;
    	unordered_map<string, int> group_string_int;
    	unordered_map<int, float> group_int_float;
    	unordered_map<float, float> group_float_float;
    	unordered_map<string, float> group_string_float;

    	RC getNextTuple_groupMaxMinSum(void *data);
    	RC getNextTuple_groupAvg(void *data);
    	RC getNextTuple_groupCount(void *data);
    	void prepareGroupMax();
    	void prepareGroupMin();
    	void prepareGroupSum();
    	void prepareGroupAvg();
    	void prepareGroupCount();
    	template <typename GR, typename AGG>
        void groupMax(unordered_map<GR, AGG> &map, const GR &gr, const AGG& agg) {
    		if (map.count(gr) == 0) {
    			map[gr] = agg;
    		} else if (map[gr] < agg){
    			map[gr] = agg;
    		}
    	}
    	template <typename GR, typename AGG>
        void groupMin(unordered_map<GR, AGG> &map, const GR &gr, const AGG& agg) {
    		if (map.count(gr) == 0) {
    			map[gr] = agg;
    		} else if (map[gr] > agg){
    			map[gr] = agg;
    		}
    	}
    	template <typename GR, typename AGG>
        void groupSum(unordered_map<GR, AGG> &map, const GR &gr, const AGG& agg) {
    		if (map.count(gr) == 0) {
    			map[gr] = agg;
    		} else {
    			map[gr] = map[gr] + agg;
    		}
    	}
    	template <typename GR>
        void groupAvg(unordered_map<GR, float> &map_sum,
        		unordered_map<GR, int> &map_count,
        		const GR &gr, float &agg) {
    		if (map_count.count(gr) == 0) {
    			map_count[gr] = 1;
    		} else {
    			map_count[gr] = map_count[gr] + 1;
    		}
    		if (map_sum.count(gr) == 0) {
    			map_sum[gr] = (float)agg;
    		} else {
    			map_sum[gr] = map_sum[gr] + (float)agg;
    		}
    	}
    	template <typename GR>
        void groupCount(unordered_map<GR, int> &map, const GR &gr) {
    		if (map.count(gr) == 0) {
    			map[gr] = 1;
    		} else {
    			map[gr] = map[gr] + 1;
    		}
    	}
};

#endif
