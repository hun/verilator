-- DESCRIPTION: Verilator: VHDL aggregate with a literal element as a signal assignment target
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A character literal in an aggregate target is not a name, so it has no signal to assign.
entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal s : bit;
begin
  (o, '0') <= ('1', s);
end architecture rtl;
