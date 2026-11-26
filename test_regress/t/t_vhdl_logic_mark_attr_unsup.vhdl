-- DESCRIPTION: Verilator: VHDL 'succ of the predefined bit type is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A predefined bit is a discrete scalar, so 'succ applies to it (IEEE 1076-2008 16.2.2).
-- Only the position attributes of bit are lowered; 'succ is reported as unsupported.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal s : bit;
begin
  o <= bit'succ(s);
end architecture rtl;
