-- DESCRIPTION: Verilator: VHDL nested record-member port association is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A formal naming an element of an element of a record port is not modelled.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  u1 : sub port map (r.a.b => s);
end architecture rtl;
