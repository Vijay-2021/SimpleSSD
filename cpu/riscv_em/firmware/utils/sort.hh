#ifndef __RISCV_SORT__
#define __RISCV_SORT__
 
// Function to perform insertion sort on subarray `a[low…high]`
template <typename Iterator, typename Compare>
void insertionsort(Iterator begin, Iterator end, Compare comp)
{
    // start from the second element in the subarray
    // (the element at index `begin` is already sorted)
    for (auto it = begin + 1; it != end; ++it)
    {
        auto value = *it;
        auto j = it;
 
        // find index `j` within the sorted subset [begin…it-1]
        // where element `value` belongs
        while (j > begin && comp(*(j - 1), value))
        {
            *j = *(j - 1);
            --j;
        }
 
        // Note that the subarray `[j…it-1]` is shifted to
        // the right by one position, i.e., `[j+1…it]`
 
        *j = value;
    }
}

template<typename Iterator>
void swap(Iterator a, Iterator b) {
    auto temp = *a;
    *a = *b;
    *b = temp;
}

template <typename Iterator, typename Compare>
int partition(Iterator begin, Iterator end, Compare comp) {
    // Pick the rightmost element as a pivot from the array
    auto pivot = *end;
 
    // elements less than the pivot will be pushed to the left of `pIndex`
    // elements more than the pivot will be pushed to the right of `pIndex`
    // equal elements can go either way
    auto pIndex = begin;
 
    // each time we find an element less than or equal to the pivot, `pIndex`
    // is incremented, and that element would be placed before the pivot.
    for (auto it = begin; it < end; ++it)
    {
        if (comp(*it, pivot))
        {
            swap(it, pIndex);
            ++pIndex;
        }
    }
 
    // swap `pIndex` with pivot
    swap(pIndex, end);
 
    // return `pIndex` (index of the pivot element)
    return pIndex - begin;
}

template <typename Iterator, typename Compare>
int randPartition(Iterator begin, Iterator end, Compare comp)
{
    // choose a random index between `[begin, end]`
    auto pivotIndex = begin + (rand64() % (end - begin + 1));
 
    // swap the end element with the element present at a random index
    swap(pivotIndex, end);
 
    // call the partition procedure
    return partition(begin, end, comp);
}

template <typename Iterator, typename Compare>
void introsort(Iterator begin, Iterator end, Compare comp) {
    // perform insertion sort if partition size is 16 or smaller
    if ((end - begin) < 16) {
        insertionsort(begin, end, comp);
    }
    else {
        // otherwise, perform Quicksort
        auto pivot = randPartition(begin, end, comp);
        introsort(begin, begin + pivot - 1, comp);
        introsort(begin + pivot + 1, end, comp);
    }
}

#endif