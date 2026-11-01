#pragma once
// Ordine dei moduli scambiabili 2..5 (0 = De-Esser, 1 = FET, 2 = EQ, 3 = Opto).
// Il parametro "chain_order" (0..23) e' l'indice di una delle 24 permutazioni, in ordine lessicografico:
// 0 = De-Esser, FET, EQ, Opto (l'ordine originale).
#include <array>
#include <algorithm>

namespace chain
{
using Order = std::array<int, 4>;

inline const std::array<Order, 24>& perms()
{
    static const std::array<Order, 24> all = []
    {
        std::array<Order, 24> a {};
        Order o { 0, 1, 2, 3 };
        int i = 0;
        do { a[(size_t) i++] = o; } while (std::next_permutation(o.begin(), o.end()));
        return a;
    }();
    return all;
}

inline Order order(int index) { return perms()[(size_t) std::clamp(index, 0, 23)]; }

inline int indexOf(const Order& o)
{
    const auto& p = perms();
    for (int i = 0; i < 24; ++i) if (p[(size_t) i] == o) return i;
    return 0;
}

// posizione (0..3) del modulo m nell'ordine
inline int slotOf(const Order& o, int m) { for (int i = 0; i < 4; ++i) if (o[(size_t) i] == m) return i; return m; }

// scambia il modulo m con quello accanto (dir = -1 sinistra, +1 destra); restituisce il nuovo indice
inline int moved(int index, int m, int dir)
{
    Order o = order(index);
    const int s = slotOf(o, m), t = s + dir;
    if (t < 0 || t > 3) return index;
    std::swap(o[(size_t) s], o[(size_t) t]);
    return indexOf(o);
}
} // namespace chain
