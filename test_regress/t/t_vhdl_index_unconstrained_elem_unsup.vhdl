-- DESCRIPTION: Verilator: index of an array with unconstrained elements
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An unconstrained function result of an array of unconstrained elements has no element width.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type ua is array (natural range <>) of bit_vector;
  function mk (n : natural) return ua is
  begin
    return ua'(0 => "01");
  end function mk;
begin
  o <= mk(0)(0)(0);
end architecture rtl;
