-- DESCRIPTION: Verilator: VHDL external name without a colon before its subtype
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The external pathname is followed by ':' and the subtype indication (IEEE 1076-2008 8.7).
-- Here the subtype follows the pathname with no colon.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  o <= << signal u.x bit >>;
end architecture rtl;
