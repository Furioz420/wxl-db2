// Immutable retail item metadata consumed by native DBC-first item accessors.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "RetailItemCatalog.hpp"

#include <atomic>

namespace wxl::runtime::db2::retailitems
{
    namespace
    {
        std::shared_ptr<const Catalog>& CurrentSlot()
        {
            static auto* slot = new std::shared_ptr<const Catalog>();
            return *slot;
        }
    }

    void Publish(std::shared_ptr<Catalog> catalog)
    {
        std::atomic_store_explicit(&CurrentSlot(),
                                   std::shared_ptr<const Catalog>(std::move(catalog)),
                                   std::memory_order_release);
    }

    std::shared_ptr<const Catalog> Current()
    {
        return std::atomic_load_explicit(&CurrentSlot(), std::memory_order_acquire);
    }
}
