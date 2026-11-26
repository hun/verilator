-- DESCRIPTION: Verilator: VHDL subprogram call with an indexed name as a formal
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A named association in a subprogram call takes a formal name. An indexed name as the
-- formal, v(0) => ..., is not supported yet.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal v : bit_vector(0 to 1);
begin
  o <= f(v(0) => '1');
end architecture rtl;
